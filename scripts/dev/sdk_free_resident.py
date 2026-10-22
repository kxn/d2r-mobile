"""One official LLDB locked SDK-metadata + two proc-pagemap snapshot.
Host-only orchestration. Never evaluates an inferior expression or calls guest code.
Import using `command script import`; root's existing owned transaction attaches,
validates APK/ELF/raw SDK receipts, and owns server/forward/save2/CAS cleanup.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import time

SDK_SHA = '1c9f198a91c0bcfee5cf8f5b53fb941895e91334dfb766ce79fc68576797f017'
SDK_ENTRY = 0x2f7524
SDK_LR = 0x2f8ae4
ALLOC_OFFSET = 0x32137a0
_ACTIVE = None


def require(ok, why):
    if not ok:
        raise ValueError(why)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record(path):
    p = Path(path)
    return dict(path=str(p.resolve()), bytes=p.stat().st_size, sha256=digest(p))


def u64(buf, offset=0):
    require(offset >= 0 and offset + 8 <= len(buf), 'short u64')
    return struct.unpack_from('<Q', buf, offset)[0]


def parse_maps(text):
    rows = []
    for line in text.splitlines():
        f = line.split(None, 5)
        require(len(f) >= 5, 'bad maps row')
        a, b = [int(v, 16) for v in f[0].split('-')]
        require(a < b, 'bad maps interval')
        rows.append(dict(start=a, end=b, perms=f[1], offset=int(f[2], 16),
                         dev=f[3], inode=int(f[4]), name=f[5] if len(f) > 5 else ''))
    return sorted(rows, key=lambda r: r['start'])


def view_identity(rows, base, length):
    require(base > 0 and length > 0 and base + length < (1 << 64), 'bad view extent')
    cursor, identity, covered = base, None, []
    for row in rows:
        if row['end'] <= cursor or row['start'] >= base + length:
            continue
        require(row['start'] <= cursor, 'view mapping hole')
        require(len(row['perms']) == 4 and row['perms'][3] == 's', 'view is not MAP_SHARED')
        key = (row['dev'], row['inode'])
        require(key[1] > 0, 'view has no backing inode')
        if identity is None:
            identity = key
        require(key == identity, 'view backing identity changed')
        require(row['offset'] + cursor - row['start'] == cursor - base, 'view backing offset mismatch')
        covered.append(row)
        cursor = min(row['end'], base + length)
        if cursor == base + length:
            break
    require(cursor == base + length, 'view mapping incomplete')
    return dict(dev=identity[0], inode=identity[1], base=base, bytes=length, mappings=covered)


def pagemap_geometry(base, length, host_page):
    require(host_page >= 4096 and host_page & (host_page - 1) == 0, 'invalid host page size')
    require(base % host_page == 0 and length > 0 and length % host_page == 0, 'pagemap view not host-page aligned')
    offset, size = (base // host_page) * 8, (length // host_page) * 8
    skip, prefix = divmod(offset, 4096)
    count = (prefix + size + 4095) // 4096
    return dict(offset=offset, bytes=size, skip=skip, count=count, prefix=prefix, raw_bytes=count * 4096)


def decode_spans(central, total_pages, table, read, check=lambda: None):
    """Decode exact original SDK fields from read-only bytes; not an allocator.
    Mirrors only GetMemStats_'s existing next-boundary traversal. No inference
    from gaps, allocated small slots, or stale malloc/free histories.
    """
    require(central > 0 and central % 4096 == 0, 'unaligned central')
    require(0 < total_pages <= (1 << 22), 'invalid SDK page count')
    require(len(table) == total_pages * 8, 'boundary table length differs')
    end = central + total_pages * 4096
    node, cursor, seen, spans = u64(table), central, set(), []
    while cursor < end:
        check()
        require(node != 0, 'null span before partition end')
        require(node not in seen, 'span cycle')
        require(node % 8 == 0 and central <= node and node + 0x30 <= end, 'span metadata pointer out of heap')
        seen.add(node)
        require(len(seen) <= total_pages, 'too many spans')
        b = read(node, 0x30)
        require(len(b) == 0x30, 'short span record')
        state, cls, begin, pages = b[0x13], b[0x12], u64(b, 0x18), u64(b, 0x20)
        require(state in (1, 2, 3), 'unknown span state ' + str(state))
        require(begin == cursor and begin % 4096 == 0, 'partition hole/overlap/unaligned begin')
        require(pages > 0 and pages <= total_pages and begin + pages * 4096 <= end, 'span size out of bounds/nonprogress')
        spans.append(dict(metadata=node, begin=begin, end=begin + pages * 4096,
                          pages=pages, state=state, allocation_class=cls))
        cursor = begin + pages * 4096
        index = (cursor - central) // 4096
        node = u64(table, index * 8) if index < total_pages else 0
    require(cursor == end and sum(s['pages'] for s in spans) == total_pages, 'incomplete span partition')
    return spans


def intersect_free(spans, primary_base, current_bytes, host_page, primary, alias, check=lambda: None):
    n = current_bytes // host_page
    require(len(primary) == n * 8 and len(alias) == n * 8, 'pagemap payload length differs')
    out = dict(free_span_bytes=0, free_whole_host_page_bytes=0, free_host_alignment_excluded_bytes=0,
               free_present_primary_bytes=0, free_present_alias_bytes=0, free_both_present_bytes=0,
               free_primary_only_present_bytes=0, free_alias_only_present_bytes=0,
               free_union_present_bytes=0, free_union_neither_present_bytes=0,
               free_primary_swap_flag_bytes=0, free_alias_swap_flag_bytes=0,
               free_span_count=0, largest_free_span_bytes=0)
    require(0 < host_page and primary_base % host_page == 0 and current_bytes % host_page == 0, 'bad intersection geometry')
    for s in spans:
        if s['state'] != 2:
            continue
        out['free_span_count'] += 1
        size = s['end'] - s['begin']
        out['free_span_bytes'] += size
        out['largest_free_span_bytes'] = max(out['largest_free_span_bytes'], size)
        lo = ((s['begin'] + host_page - 1) // host_page) * host_page
        hi = (s['end'] // host_page) * host_page
        hi = max(lo, hi)
        require(primary_base <= lo <= hi <= primary_base + current_bytes, 'free range outside primary')
        out['free_host_alignment_excluded_bytes'] += size - (hi - lo)
        out['free_whole_host_page_bytes'] += hi - lo
        for a in range(lo, hi, host_page):
            idx = (a - primary_base) // host_page
            if idx % 4096 == 0:
                check()
            p, q = u64(primary, idx * 8), u64(alias, idx * 8)
            pp, qp = bool(p & (1 << 63)), bool(q & (1 << 63))
            for key, condition in [
                ('free_present_primary_bytes', pp), ('free_present_alias_bytes', qp),
                ('free_both_present_bytes', pp and qp),
                ('free_primary_only_present_bytes', pp and not qp),
                ('free_alias_only_present_bytes', qp and not pp),
                ('free_union_present_bytes', pp or qp),
                ('free_union_neither_present_bytes', not pp and not qp),
                ('free_primary_swap_flag_bytes', bool(p & (1 << 62))),
                ('free_alias_swap_flag_bytes', bool(q & (1 << 62)))]:
                if condition:
                    out[key] += host_page
    require(out['free_union_present_bytes'] + out['free_union_neither_present_bytes'] == out['free_whole_host_page_bytes'], 'intersection partition')
    return out


def _unsigned(v):
    import lldb
    require(v.IsValid() and v.GetError().Success(), 'invalid SBValue ' + str(v.GetName()))
    err = lldb.SBError()
    n = v.GetValueAsUnsigned(err)
    require(err.Success(), 'failed SBValue read ' + str(v.GetName()) + ': ' + str(err))
    return n


def _member(v, name):
    q = v.GetChildMemberWithName(name)
    require(q.IsValid() and q.GetError().Success(), 'missing typed field ' + name)
    return q


def _global(target, name, allowed_files):
    values, candidates = target.FindGlobalVariables(name, 100), []
    for i in range(values.GetSize()):
        v = values.GetValueAtIndex(i)
        if Path(str(v.GetDeclaration().GetFileSpec())).name in allowed_files:
            candidates.append(v)
    require(len(candidates) == 1, 'non-unique named global ' + name + ': ' + str([str(v.GetDeclaration()) for v in candidates]))
    return candidates[0]


def _on_entry(frame, bp_loc, _dict):
    """Official debugger register callback, no inferior expression/evaluation."""
    active = _ACTIVE
    if active is None:
        return True
    try:
        if _unsigned(frame.FindRegister('x0')) != active['central']:
            return False
        if _unsigned(frame.FindRegister('x30')) != active['sdk'] + SDK_LR:
            return False
        if active.get('nvn_entry_gate'):
            gate = active['nvn_entry_gate']
            active['nvn_qualification']['natural_entries_examined'] += 1
            raw = _read_small(frame.GetThread().GetProcess(), gate['address'], gate['bytes'])
            try:
                active['nvn_entry_predicate'](raw)
            except ValueError:
                active['nvn_qualification']['nonzero_mutex_entries_declined'] += 1
                active['nvn_qualification']['last_declined_mutex_hex'] = raw.hex()
                active['qualification_declined'] = dict(stop_id=frame.GetThread().GetProcess().GetStopID(),
                    thread=frame.GetThread().GetThreadID(), breakpoint=bp_loc.GetBreakpoint().GetID(), location=bp_loc.GetID())
                # Do not let callback returnFalse create a transient restarted
                # stop that races the asynchronous owner. Its explicit loop
                # validates this exact stop and alone issues Continue().
                return True
            active['nvn_qualification']['accepted_mutex_hex'] = raw.hex()
        bp_loc.GetBreakpoint().SetEnabled(False)
        active['thread'] = frame.GetThread().GetThreadID()
        active['matched'] = True
        return True
    except Exception as error:
        active['callback_error'] = repr(error)
        bp_loc.GetBreakpoint().SetEnabled(False)
        return True


def _resume_qualification_declined(process, bp, active, last_stop, deadline):
    """Resume only our exact deliberately declined main-SDK stop, once.
    This is official debugger event control, not a new target polling loop.
    """
    import lldb
    require(time.monotonic() < deadline, 'natural entry wait budget expired before qualified resume')
    require(not active.get('matched') and not active.get('callback_error'), 'not a clean qualification decline')
    decline = active.get('qualification_declined')
    require(decline and process.GetState() == lldb.eStateStopped and
            process.GetStopID() == decline['stop_id'] > last_stop, 'declined stop is stale or not stopped')
    stopped = [process.GetThreadAtIndex(i) for i in range(process.GetNumThreads())
               if process.GetThreadAtIndex(i).GetStopReason() not in (lldb.eStopReasonInvalid, lldb.eStopReasonNone)]
    require(len(stopped) == 1, 'unrelated/multiple stop reasons at qualification decline')
    thread = stopped[0]
    require(thread.GetThreadID() == decline['thread'] and thread.GetStopReason() == lldb.eStopReasonBreakpoint and
            thread.GetStopReasonDataCount() == 2 and thread.GetStopReasonDataAtIndex(0) == bp.GetID() == decline['breakpoint'] and
            thread.GetStopReasonDataAtIndex(1) == decline['location'], 'qualification decline is not our exact original breakpoint')
    frame = thread.GetFrameAtIndex(0)
    require(_unsigned(frame.FindRegister('pc')) == active['sdk'] + SDK_ENTRY and
            _unsigned(frame.FindRegister('x0')) == active['central'] and
            _unsigned(frame.FindRegister('x30')) == active['sdk'] + SDK_LR, 'declined SDK register identity changed')
    stop = decline['stop_id']
    active['nvn_qualification']['last_declined_stop'] = decline
    active.pop('qualification_declined')
    error = process.Continue()
    require(error.Success(), 'explicit qualification Continue failed: ' + str(error))
    q = active['nvn_qualification']
    q['declined_stops_explicitly_resumed'] = q.get('declined_stops_explicitly_resumed', 0) + 1
    return stop


class Snapshot:
    def __init__(self, process, config, output):
        import lldb
        self.process, self.config, self.output = process, config, Path(output)
        self.pid, self.stop = process.GetProcessID(), process.GetStopID()
        self.deadline, self.raw_bytes, self.metadata_bytes = time.monotonic() + 60, 0, 0
        self.pages, self.files = {}, []
        self.lldb = lldb

    def check(self):
        require(time.monotonic() < self.deadline, '60s stopped-state acquisition budget exceeded')
        require(self.process.GetState() == self.lldb.eStateStopped and self.process.GetStopID() == self.stop and self.process.GetProcessID() == self.pid, 'not the same owned debugger stop')

    def budget(self, size):
        self.check()
        require(size >= 0 and self.raw_bytes + size <= 40 * 1024 ** 2, '40MiB raw acquisition budget exceeded')
        self.raw_bytes += size

    def save(self, name, data):
        path = self.output / name
        path.write_bytes(data)
        r = record(path)
        self.files.append(r)
        return r

    def read(self, address, length, name):
        self.budget(length)
        b = bytearray()
        try:
            while len(b) < length:
                self.check()
                size = min(65536, length - len(b))
                err = self.lldb.SBError()
                chunk = self.process.ReadMemory(address + len(b), size, err)
                b.extend(chunk)
                require(err.Success() and len(chunk) == size, 'ReadMemory failed/short at ' + hex(address + len(b) - len(chunk)) + ': ' + str(err))
            self.check()
        finally:
            # Preserve a failed raw prefix as such; never fill short reads with0.
            self.save(name, bytes(b))
        return bytes(b)

    def metadata(self, address, length):
        out = bytearray()
        while length:
            page = address & -4096
            if page not in self.pages:
                require(self.metadata_bytes + 4096 <= 16 * 1024 ** 2, '16MiB unique metadata budget exceeded')
                self.pages[page] = self.read(page, 4096, 'span-metadata-' + format(page, 'x') + '.bin')
                self.metadata_bytes += 4096
            offset, size = address - page, min(length, 4096 - (address - page))
            out.extend(self.pages[page][offset:offset + size])
            address += size
            length -= size
        return bytes(out)

    def app(self, argv, name, raw_expected=None):
        env = os.environ.copy()
        env.pop('LD_LIBRARY_PATH', None)
        env.pop('PYTHONPATH', None)
        cmd = [self.config['adb'], '-s', self.config['serial'], 'exec-out', 'run-as', self.config['package']] + argv
        reserved = raw_expected if raw_expected is not None else (1024 ** 2 if name == 'maps.txt' else 4096)
        self.budget(reserved)
        timeout = min(15, max(0.01, self.deadline - time.monotonic()))
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env)
        except subprocess.TimeoutExpired as e:
            self.save(name, e.stdout or b'')
            self.save(name + '.stderr', e.stderr or b'')
            raise
        self.save(name, p.stdout)
        self.save(name + '.stderr', p.stderr)
        self.check()
        require(len(p.stdout) <= reserved, 'proc/tool output exceeds bounded reservation ' + name)
        self.raw_bytes -= reserved - len(p.stdout)
        require(p.returncode == 0, 'proc/tool command failed ' + name + ': ' + p.stderr.decode(errors='replace'))
        if raw_expected is not None:
            require(len(p.stdout) == raw_expected, 'proc/tool command short/extra output ' + name)
        return p.stdout, dict(argv=cmd, returncode=p.returncode)

    def pagemap(self, base, length, page, name):
        g = pagemap_geometry(base, length, page)
        b, cmd = self.app(['dd', 'if=/proc/' + str(self.pid) + '/pagemap', 'bs=4096',
                           'skip=' + str(g['skip']), 'count=' + str(g['count']), 'status=none'], name + '-pagemap.raw', g['raw_bytes'])
        data = b[g['prefix']:g['prefix'] + g['bytes']]
        require(len(data) == g['bytes'], 'pagemap slicing length')
        # Raw page-aligned original is authoritative; no second payload copy saved.
        return data, dict(geometry=g, command=cmd)


def _read_small(process, address, size):
    import lldb
    err = lldb.SBError()
    b = process.ReadMemory(address, size, err)
    require(err.Success() and len(b) == size, 'initial identity ReadMemory failed at ' + hex(address) + ': ' + str(err))
    return b


def _read_heap(target):
    info, values = {}, {}
    for name in ['g_heap', 'g_heap_cur', 'g_heap_cap', 'g_heap_import_alias']:
        value = _global(target, name, {'guest_heap_backing.inc', 'hle_svc.c'})
        values[name] = _unsigned(value)
        info[name] = dict(value=values[name], declaration=str(value.GetDeclaration()), type=value.GetTypeName())
    return values, info


def _read_modules(target):
    count = _global(target, 'g_nmod', {'loader.c'})
    mods = _global(target, 'g_mod', {'loader.c'})
    n = _unsigned(count)
    require(0 < n <= mods.GetNumChildren() and n <= 32, 'g_nmod out of actual DWARF array bounds')
    result = []
    for i in range(n):
        v = mods.GetChildAtIndex(i)
        name = _member(v, 'name')
        chars = bytes(_unsigned(name.GetChildAtIndex(k)) for k in range(16))
        text = chars.split(b'\0', 1)[0].decode('ascii')
        result.append(dict(name=text, base=_unsigned(_member(v, 'base')), bytes=_unsigned(_member(v, 'size'))))
    require(sum(m['name'] == 'main' for m in result) == 1 and sum(m['name'] == 'sdk' for m in result) == 1, 'main/sdk module names not unique')
    return result, dict(g_nmod=str(count.GetDeclaration()), g_mod=str(mods.GetDeclaration()), module_t_bytes=mods.GetType().GetArrayElementType().GetByteSize())


def _load_nvn_companion(pin, config):
    require({'enabled', 'mode', 'module', 'ELF_sha256', 'ELF_BuildID'} <= set(pin) <=
            {'enabled', 'mode', 'module', 'ELF_sha256', 'ELF_BuildID', 'qualify_unlocked', 'libc_sha256'} and pin['enabled'] is True,
            'optional nvn_pools config fields/enable differ')
    require(type(pin.get('qualify_unlocked', False)) is bool, 'optional NVN qualification must be a bool')
    require(pin['ELF_sha256'] == config['elf']['sha256'] and
            pin['ELF_BuildID'].replace('-', '').lower() == config['elf']['buildid'].replace('-', '').lower(),
            'optional NVN metadata ELF pin differs from actual SDK snapshot')
    module = pin['module']
    require(set(module) == {'path', 'sha256'} and digest(module['path']) == module['sha256'],
            'optional NVN companion source hash differs')
    spec = importlib.util.spec_from_file_location('owned_sdk_nvn_pool_registry', module['path'])
    companion = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(companion)
    return companion


def _optional_nvn_pools(target, snap, pin, heap_begin, heap_end):
    """Independent optional result: a busy/unknown registry never loses SDK data.
    The companion is an explicitly pinned host-only module, not target code.
    Root's existing controller still owns server, forward and lifecycle.
    """
    result = dict(complete=False, inferior_calls=False,
                  status='SAME_SDK_STOP_NVN_POOL_REGISTRY_UNAVAILABLE', errors=[])
    try:
        companion = _load_nvn_companion(pin, snap.config)
        result['companion_source'] = record(pin['module']['path'])
        snap.config['_sdk_heap_begin'], snap.config['_sdk_heap_end'] = heap_begin, heap_end
        result.update(companion.capture(target, snap, pin))
    except Exception as error:
        result['errors'].append(repr(error))
    return result


def run(debugger, config):
    """Called by root's existing owned LLDB transaction after initial attach.
    Required config: output_dir, pid, elf{path,sha256,buildid}, sdk_original{path,
    sha256}, sdk_flat{path,sha256}, adb, serial, package. Root supplies matching
    original-NSO deployment/ELF/APK receipt in deployment_identity (record only).
    Optional natural_wait_seconds <=60. No device launch/control in this hook.
    """
    import lldb
    global _ACTIVE
    target, output = debugger.GetSelectedTarget(), Path(config['output_dir'])
    process = target.GetProcess()
    r = dict(tool='official LLDB SBValue/ReadMemory + proc pagemap one locked SDK snapshot',
             complete=False, matched=False, inferior_calls=False, target_expressions=False,
             sampler=False, writes=False, errors=[], pid=process.GetProcessID(),
             deployment_identity=config.get('deployment_identity'), raw_files=[],
             interpretation='Whole SDK-free CPU mapping present lower bound only; not complete physical residency or safe reclaimability.')
    bp = None
    snap = None
    was_async = debugger.GetAsync()
    try:
        output.mkdir(parents=True, exist_ok=True)
        require(not (output / 'sdk-free-resident.json').exists(), 'refuse an existing snapshot output')
        require(process.IsValid() and process.GetProcessID() == config['pid'] and process.GetState() == lldb.eStateStopped, 'no matching owned live initial stop')
        require(target.GetTriple().startswith('aarch64'), 'not native aarch64 target')
        elf = config['elf']
        require(digest(elf['path']) == elf['sha256'], 'local formal ELF SHA differs')
        module = target.FindModule(lldb.SBFileSpec('libdmhost_android.so'))
        require(module.IsValid() and module.GetUUIDString().replace('-', '').lower() == elf['buildid'].replace('-', '').lower(), 'actual target host BuildID differs')
        r['host'] = dict(**record(elf['path']), buildid=module.GetUUIDString())
        require(config['sdk_original']['sha256'] == SDK_SHA and digest(config['sdk_original']['path']) == SDK_SHA, 'original SDK input hash differs; static layout not qualified')
        flat = Path(config['sdk_flat']['path'])
        require(digest(flat) == config['sdk_flat']['sha256'], 'original SDK flat source hash differs')
        entry = flat.read_bytes()[SDK_ENTRY:SDK_ENTRY + 32]
        require(len(entry) == 32, 'original SDK flat entry short')
        r['original_sdk'] = record(config['sdk_original']['path'])
        r['sdk_flat'] = record(flat)
        mods, decl = _read_modules(target)
        heap, heap_decl = _read_heap(target)
        r.update(modules=mods, module_declarations=decl, initial_heap=heap_decl)
        main, sdk = [next(m for m in mods if m['name'] == n) for n in ['main', 'sdk']]
        allocator = main['base'] + ALLOC_OFFSET
        require(ALLOC_OFFSET + 0x20 <= main['bytes'] and SDK_ENTRY + 32 <= sdk['bytes'], 'static metadata/code offsets outside modules')
        a = _read_small(process, allocator, 0x20)
        require(a[0] == 1, 'main StandardAllocator is not initialized')
        central = u64(a, 0x18)
        require(heap['g_heap'] <= central < heap['g_heap'] + heap['g_heap_cur'] and central % 4096 == 0, 'central pointer outside actual heap/alignment')
        require(_read_small(process, sdk['base'] + SDK_ENTRY, 32) == entry, 'live SDK entry differs from qualified source')
        _ACTIVE = dict(central=central, sdk=sdk['base'], matched=False)
        if config.get('nvn_pools', {}).get('qualify_unlocked', False):
            companion = _load_nvn_companion(config['nvn_pools'], config)
            _ACTIVE['nvn_entry_gate'] = companion.prepare_entry_gate(target, config, config['nvn_pools'])
            _ACTIVE['nvn_entry_predicate'] = lambda raw, f=companion.normal_resources_unlocked, mode=config['nvn_pools']['mode']: f(raw, mode)
            _ACTIVE['nvn_qualification'] = dict(natural_entries_examined=0, nonzero_mutex_entries_declined=0)
        bp = target.BreakpointCreateByAddress(sdk['base'] + SDK_ENTRY)
        require(bp.IsValid() and bp.GetNumLocations() == 1, 'natural SDK entry breakpoint unresolved')
        bp.SetScriptCallbackFunction(__name__ + '._on_entry')
        listener = lldb.SBListener('owned-sdk-free-resident-natural-entry')
        listener.StartListeningForEvents(process.GetBroadcaster(), lldb.SBProcess.eBroadcastBitStateChanged)
        initial_stop = process.GetStopID()
        debugger.SetAsync(True)
        err = process.Continue()
        require(err.Success(), 'Continue failed: ' + str(err))
        wait_seconds = config.get('natural_wait_seconds', 60)
        require(0 < wait_seconds <= 60, 'natural entry wait budget outside0..60s')
        wait_end = time.monotonic() + wait_seconds
        while True:
            state = process.GetState()
            if state == lldb.eStateStopped and process.GetStopID() > initial_stop:
                if _ACTIVE.get('qualification_declined') and not _ACTIVE.get('callback_error'):
                    initial_stop = _resume_qualification_declined(process, bp, _ACTIVE, initial_stop, wait_end)
                    continue
                require(_ACTIVE.get('matched') and not _ACTIVE.get('callback_error'), 'unrelated stop/callback error: ' + str(_ACTIVE))
                r['matched'] = True
                break
            require(state not in (lldb.eStateExited, lldb.eStateDetached, lldb.eStateCrashed), 'process exited/detached/crashed before natural stop')
            left = wait_end - time.monotonic()
            if left <= 0:
                r['no_hit'] = True
                raise TimeoutError('no natural locked maincentral call within tool budget')
            event = lldb.SBEvent()
            listener.WaitForEvent(1, event)
        snap = Snapshot(process, config, output)
        thread = process.GetThreadByID(_ACTIVE['thread'])
        require(thread.IsValid(), 'matched thread missing')
        process.SetSelectedThread(thread)
        frame = thread.GetFrameAtIndex(0)
        require(_unsigned(frame.FindRegister('pc')) == sdk['base'] + SDK_ENTRY and _unsigned(frame.FindRegister('x0')) == central and _unsigned(frame.FindRegister('x30')) == sdk['base'] + SDK_LR, 'natural entry register identity changed')
        heap, heap_decl = _read_heap(target)
        r.update(stop_id=snap.stop, thread=thread.GetThreadID(), heap=heap_decl,
                 registers={n: _unsigned(frame.FindRegister(n)) for n in ['pc', 'x0', 'x30']},
                 allocator=allocator, central=central)
        r['threads'] = [dict(tid=process.GetThreadAtIndex(i).GetThreadID(), name=process.GetThreadAtIndex(i).GetName())
                        for i in range(process.GetNumThreads())]
        r['matched_backtrace'] = []
        for k in range(8):
            caller = thread.GetFrameAtIndex(k)
            if not caller.IsValid():
                break
            r['matched_backtrace'].append(str(caller))
        host_page_text, page_cmd = snap.app(['getconf', 'PAGESIZE'], 'host-page-size.txt')
        host_page = int(host_page_text.strip())
        primary, alias, cur = heap['g_heap'], heap['g_heap_import_alias'], heap['g_heap_cur']
        require(0 < cur <= heap['g_heap_cap'] and alias > 0 and alias != primary and cur <= 4 * 1024 ** 3, 'heap globals inconsistent/shared alias absent')
        maps, maps_cmd = snap.app(['cat', '/proc/' + str(process.GetProcessID()) + '/maps'], 'maps.txt')
        rows = parse_maps(maps.decode())
        pview, aview = view_identity(rows, primary, cur), view_identity(rows, alias, cur)
        require((pview['dev'], pview['inode']) == (aview['dev'], aview['inode']), 'primary/importalias do not share backing identity')
        r.update(host_page_size=host_page, views=dict(primary=pview, import_alias=aview), commands=dict(page_size=page_cmd, maps=maps_cmd))
        # Capture kernel PTE flags before touching metadata in the heap.
        pp, pcmd = snap.pagemap(primary, cur, host_page, 'primary')
        ap, acmd = snap.pagemap(alias, cur, host_page, 'import-alias')
        r['pagemaps'] = dict(primary=pcmd, import_alias=acmd)
        a = snap.read(allocator, 64, 'main-allocator.bin')
        require(a[0] == 1 and u64(a, 0x18) == central, 'mainallocator changed since initial identity')
        c = snap.read(central, 256, 'central-header.bin')
        pages, boundary = u64(c), u64(c, 8)
        end = central + pages * 4096
        require(c[0x2c] == 0 and pages > 0 and primary <= central < end <= primary + cur, 'central virtual mode/extent differs')
        require(central <= boundary and boundary + pages * 8 <= end, 'boundary table outside actual central interval')
        table = snap.read(boundary, pages * 8, 'span-boundary-table.bin')
        spans = decode_spans(central, pages, table, snap.metadata, snap.check)
        r.update(central_pages=pages, boundary_table=boundary,
                 free_spans=[s for s in spans if s['state'] == 2],
                 partition=dict(span_count=len(spans), pages=sum(s['pages'] for s in spans),
                                state_pages={str(state): sum(s['pages'] for s in spans if s['state'] == state) for state in (1, 2, 3)},
                                full_replay_source='span-boundary-table.bin + address-named span-metadata-*.bin'),
                 partition_complete=True, sdk_free_whole_spans=sum(s['end'] - s['begin'] for s in spans if s['state'] == 2))
        snap.check()
        r['intersection'] = intersect_free(spans, primary, cur, host_page, pp, ap, snap.check)
        snap.check()
        if 'nvn_pools' in config:
            r['nvn_pools'] = _optional_nvn_pools(target, snap, config['nvn_pools'], primary, primary + cur)
        if 'main_arena_origins' in config:
            r['main_arena_origins'] = dict(complete=False, inferior_calls=False,
                status='SAME_SDK_STOP_MAIN_ARENA_ORIGINS_UNAVAILABLE', errors=[])
            try:
                require('nvn_pools' in config, 'arena ownership needs same-stop Pool metadata')
                companion = _load_nvn_companion(config['nvn_pools'], config)
                r['main_arena_origins'].update(companion.capture_main_arenas(target, snap,
                    config['main_arena_origins'], main, heap, spans, host_page, pp, ap, r['nvn_pools']))
            except Exception as error:
                r['main_arena_origins']['errors'].append(repr(error))
        snap.check()
        r.update(complete=True, same_debugger_stop=True, metadata_bytes=snap.metadata_bytes,
                 raw_acquisition_bytes=snap.raw_bytes, raw_files=snap.files)
    except BaseException as error:
        r['errors'].append(repr(error))
    finally:
        if _ACTIVE is not None and _ACTIVE.get('nvn_entry_gate'):
            r['nvn_entry_qualification'] = dict(gate=_ACTIVE['nvn_entry_gate'], **_ACTIVE['nvn_qualification'])
        if bp is not None and bp.IsValid():
            target.BreakpointDelete(bp.GetID())
        _ACTIVE = None
        if snap is not None:
            r.update(raw_files=snap.files, raw_acquisition_bytes=snap.raw_bytes, metadata_bytes=snap.metadata_bytes)
        state = process.GetState()
        if process.IsValid() and state not in (lldb.eStateExited, lldb.eStateDetached):
            if state != lldb.eStateStopped:
                err = process.Stop()
                r['interrupt'] = str(err)
                if err.Fail():
                    r['errors'].append('interrupt failed; owned controller must recover: ' + str(err))
            err = process.Detach()
            r['detached'] = err.Success()
            r['detach'] = str(err)
            if err.Fail():
                r['errors'].append('detach failed; owned controller must recover: ' + str(err))
                r['complete'] = False
        else:
            r['already_exited_or_detached'] = True
        debugger.SetAsync(was_async)
        r['final_process_state'] = process.GetState()
        r['PID'] = r['pid']
        r['ELF_sha256'] = config.get('elf', {}).get('sha256')
        r['status'] = 'SDK_FREE_SPAN_CPU_VIEW_SNAPSHOT_COMPLETE' if r['complete'] and not r['errors'] else 'SDK_FREE_SPAN_CPU_VIEW_SNAPSHOT_INCOMPLETE'
        (output / 'sdk-free-resident.json').write_text(json.dumps(r, indent=2) + '\n')
        print('SDK_FREE_RESIDENT_STOP ' + json.dumps({k: r.get(k) for k in ['complete', 'matched', 'pid', 'detached', 'errors']}), flush=True)
    return r


def snapshot(debugger):
    """Root's existing run-debugger entry; config/env identity must agree.
    Root owns APK/raw-SDK provenance, actual mapped ELF verification, owned
    server/forward/lifecycle cleanup. This hook acquires and always detaches.
    """
    import lldb
    path = os.environ['DM_SDK_FREE_CONFIG']
    output = Path(os.environ['DM_SDK_FREE_OUTPUT'])
    process = debugger.GetSelectedTarget().GetProcess()
    try:
        c = json.loads(Path(path).read_text())
        pid = int(os.environ['DM_SDK_FREE_PID'])
        require(c['serial'] == os.environ['DM_SDK_FREE_SERIAL'], 'config/env serial differs')
        require(c['ELF_sha256'] == os.environ['DM_SDK_FREE_ELF_SHA256'], 'config/env ELF SHA differs')
        require(c['ELF_BuildID'].replace('-', '').lower() == os.environ['DM_SDK_FREE_BUILD_ID'].replace('-', '').lower(), 'config/env host BuildID differs')
        require(c['APK_sha256'] and c['canonical_source_ready_sha256'], 'config lacks existing APK/canonical identity')
        normalized = dict(c, output_dir=str(output), pid=pid,
                          elf=dict(path=c['ELF'], sha256=c['ELF_sha256'], buildid=c['ELF_BuildID']),
                          adb=os.environ['DM_SDK_FREE_ADB'], package=c.get('package', 'com.d2rmobile.nativeport'),
                          deployment_identity=dict(config=record(path), APK_sha256=c['APK_sha256'],
                                                   canonical_source_ready_sha256=c['canonical_source_ready_sha256']))
        return run(debugger, normalized)
    except BaseException as error:
        r = dict(complete=False, status='SDK_FREE_SPAN_CPU_VIEW_SNAPSHOT_INCOMPLETE',
                 inferior_calls=False, target_expressions=False, PID=process.GetProcessID(),
                 errors=[repr(error)], config_path=path)
        if process.IsValid() and process.GetState() not in (lldb.eStateExited, lldb.eStateDetached):
            if process.GetState() != lldb.eStateStopped:
                err = process.Stop()
                r['interrupt'] = str(err)
            err = process.Detach()
            r['detached'] = err.Success()
            r['detach'] = str(err)
            if err.Fail():
                r['errors'].append('detach failed; owned controller must recover: ' + str(err))
        else:
            r['already_exited_or_detached'] = True
        output.mkdir(parents=True, exist_ok=True)
        (output / 'sdk-free-resident.json').write_text(json.dumps(r, indent=2) + '\n')
        print('SDK_FREE_RESIDENT_STOP ' + json.dumps(r), flush=True)
        return r
