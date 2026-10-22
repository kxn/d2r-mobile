"""Optional f75 NVN Pool metadata at the existing SDK allocator all-stop.
Official LLDB typed values over saved read-only bytes, no target calls/locks.
The owning sdk_free_resident Snapshot supplies the unchanged60s/40MiB limits.
"""
import hashlib
import json
import struct
from pathlib import Path

MODE = 'f75-bionic-normal-resources-zero-v1'
MODE_Q735 = 'f75-q735-libc-02969f76-state16-v2'
Q735_LIBC_SHA = '02969f76be8e6ed8a44be78083735801448a6e0f972792da63b93e4a6de8d5b0'
ELF_SHA = '4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618'
BUILD_ID = '6e6f12c5c5f7a858704a84ee294b19d95aa3f894'
MODE_930_Q735 = '930-q735-libc-02969f76-state16-v1'
ELF_930_SHA = 'bfbcdae5a4cb03ae81a102d61495276163c738e2ac65e62938d8b10d3b33b41f'
BUILD_930_ID = 'dadc99a83587a0606369d35ac3b0abc0eb357d0e'
MAX_POOLS, MAX_BACKINGS, MAX_BYTES = 64, 4096, 1024 * 1024


def require(ok, why):
    if not ok:
        raise ValueError(why)


def member(value, name):
    v = value.GetChildMemberWithName(name)
    require(v.IsValid() and v.GetError().Success(), 'missing actual typed member ' + name)
    return v


def integer(value):
    import lldb
    require(value.IsValid() and value.GetError().Success(), 'invalid typed value')
    e = lldb.SBError()
    result = value.GetValueAsUnsigned(e)
    require(e.Success(), 'failed typed value read: ' + str(e))
    return result


def fields(value, names):
    return {name: integer(member(value, name)) for name in names}


def unique_global(target, name, source):
    vs = target.FindGlobalVariables(name, 100)
    matches = [vs.GetValueAtIndex(i) for i in range(vs.GetSize())
               if Path(str(vs.GetValueAtIndex(i).GetDeclaration().GetFileSpec())).name == source]
    require(len(matches) == 1, 'missing/ambiguous actual global ' + name)
    return matches[0]


def normal_resources_unlocked(raw, mode=MODE):
    # f75 explicitly uses PTHREAD_MUTEX_INITIALIZER (normal/private) for V.
    # AOSP LP64 Bionic's first atomic uint16 state is0 when unlocked,1/2 when
    # owned. No parsing owner TIDs, recursion, PI, trylock or futex activity.
    # Old v1 requires the entire original40-byte zero initializer. The exact
    # Q735 vendor image has a separately pinned diagnostic-halfword mode below.
    require(len(raw) == 40, 'resources mutex ABI differs')
    if mode in (MODE_Q735, MODE_930_Q735):
        # Exact API36 Q735 libc disassembly: successful normal lock/trylock
        # stores diagnostic TLS tid16 at+2; normal unlock SWP2 clears only+0.
        # Its stale+2 word is not ownership. Preserve/record it, never modify it.
        require(raw[:2] == bytes(2) and raw[4:] == bytes(36),
                'V.resources state16 is owned or reserved bytes are not qualified')
    else:
        require(mode == MODE, 'unknown mutex ABI mode')
        require(raw == bytes(40), 'V.resources is held or not a qualified normal zero mutex')


def prepare_entry_gate(target, config, pin):
    """Cold typed address resolution; optional callback later reads only40B."""
    require(pin['mode'] in (MODE, MODE_Q735, MODE_930_Q735), 'NVN metadata mode not qualified')
    if pin['mode'] in (MODE_Q735, MODE_930_Q735):
        require(pin.get('libc_sha256') == Q735_LIBC_SHA, 'actual Q735 libc ABI pin missing/differs')
    else:
        require('libc_sha256' not in pin, 'old mutex mode must not imply another libc ABI')
    expected_sha, expected_id = (ELF_930_SHA, BUILD_930_ID) if pin['mode'] == MODE_930_Q735 else (ELF_SHA, BUILD_ID)
    require(pin['ELF_sha256'] == expected_sha == config['elf']['sha256'], 'NVN metadata source/ELF hash not qualified')
    require(pin['ELF_BuildID'].replace('-', '').lower() == expected_id == config['elf']['buildid'].replace('-', '').lower(), 'NVN metadata source/BuildID not qualified')
    v = unique_global(target, 'V', 'nvn_vk.c')
    mutex = member(v, 'resources')
    require(mutex.GetType().GetByteSize() == 40, 'actual pthread_mutex_t size differs')
    return dict(address=mutex.GetLoadAddress(), bytes=40,
                V_declaration=str(v.GetDeclaration()), mutex_type=mutex.GetTypeName(),
                mode=pin['mode'], libc_sha256=pin.get('libc_sha256'),
                rule='all40zero' if pin['mode'] == MODE else 'exact pinned Q735 libc: state16zero, bytes4:40zero; bytes2:4 diagnostic only')


def validate_registry(pools, head_before, epoch_before, adds_before,
                      head_after, epoch_after, adds_after):
    """Validate a completed list, not infer it from cumulative epoch counters."""
    require((head_before, epoch_before, adds_before) == (head_after, epoch_after, adds_after),
            'registry head/epoch/add counter changed at all-stop')
    require((not pools and head_before == 0) or (pools and pools[0]['metadata'] == head_before), 'registry head/list mismatch')
    require(len(pools) <= MAX_POOLS, 'Pool count budget')
    ids, addresses, nodes = set(), set(), set()
    count = 0
    intervals = []
    for index, p in enumerate(pools):
        require(p['metadata'] not in addresses, 'Pool list cycle/duplicate metadata')
        addresses.add(p['metadata'])
        require(p['next'] == (pools[index + 1]['metadata'] if index + 1 < len(pools) else 0), 'Pool next/list mismatch')
        require(0 < p['identity'] <= epoch_before and p['identity'] not in ids, 'Pool identity zero/duplicate/unpublished')
        ids.add(p['identity'])
        require(p['guest'] > 0 and p['size'] > 0 and p['guest'] + p['size'] < (1 << 64), 'Pool logical interval invalid')
        require(p['shared_closing'] == 0, 'closing Pool is not stable registered ownership')
        require(p['shared'] in (0, 1) and p['shared_lazy_eligible'] in (0, 1), 'Pool shared flag invalid')
        intervals.append((p['guest'], p['guest'] + p['size']))
        prior_end = 0
        for chain in ['backings', 'shared_covers']:
            for j, b in enumerate(p[chain]):
                count += 1
                require(count <= MAX_BACKINGS and b['metadata'] not in nodes, 'Buffer chain cycle/duplicate/budget')
                nodes.add(b['metadata'])
                require(b['next'] == (p[chain][j + 1]['metadata'] if j + 1 < len(p[chain]) else 0), 'Buffer next/list mismatch')
                require(0 <= b['begin'] < b['end'] <= p['size'], 'Buffer logical interval outside Pool')
                require(0 < b['generation'] <= p['backing_generation'], 'Buffer generation not published')
                require(b['buffer']['handle'] and b['buffer']['memory'], 'Buffer Vulkan handle/memory not published')
                require(b['buffer']['size'] == b['end'] - b['begin'], 'Buffer size/origin mismatch')
                if p['shared']:
                    require(b['buffer']['shared_import'] == 1 and b['buffer']['map'] == p['guest'] + b['begin'], 'shared Buffer import/CPU origin mismatch')
                if chain == 'backings':
                    require(b['begin'] >= prior_end, 'canonical Buffer list unsorted/overlapping')
                    prior_end = b['end']
            # Cover nodes may overlap canonical nodes and one another. Do not
            # apply the canonical sorted/disjoint rule to those immutable aliases.
        if p['shared_lazy_eligible']:
            combined = p['backings'] + p['shared_covers']
            require(len(combined) == p['shared_import_nodes'] and len(p['shared_covers']) == p['shared_cover_nodes'], 'lazy import counts are in publication middle-state')
            require(sum(b['end'] - b['begin'] for b in combined) == p['shared_import_bytes'], 'lazy imported byte count differs')
    intervals.sort()
    require(all(a[1] <= b[0] for a, b in zip(intervals, intervals[1:])), 'registered Pool logical ranges overlap')
    return dict(pool_count=len(pools), buffer_node_count=count,
                logical_pool_bytes=sum(p['size'] for p in pools),
                buffer_import_interval_bytes=sum(b['end'] - b['begin'] for p in pools for chain in ['backings', 'shared_covers'] for b in p[chain]),
                native_allocation_lists_not_traversed=sum(bool(p['native_allocations']) for p in pools))


def capture(target, snapshot, pin):
    import lldb
    prepare_entry_gate(target, snapshot.config, pin)
    snapshot.check()
    declarations = {}
    values = {}
    for name in ['V', 'pools', 'pool_registry_epoch', 'pool_adds']:
        v = unique_global(target, name, 'nvn_vk.c')
        values[name] = v
        declarations[name] = dict(declaration=str(v.GetDeclaration()), type=v.GetTypeName(),
                                  address=v.GetLoadAddress(), bytes=v.GetType().GetByteSize())
    mutex = member(values['V'], 'resources')
    require(mutex.GetType().GetByteSize() == 40, 'actual pthread_mutex_t size differs')
    ptype = values['pools'].GetType().GetPointeeType()
    pfields = {ptype.GetFieldAtIndex(i).GetName(): ptype.GetFieldAtIndex(i).GetType() for i in range(ptype.GetNumberOfFields())}
    btype = pfields['backings'].GetPointeeType()
    require(ptype.GetByteSize() == 448 and btype.GetByteSize() == 152, 'actual Pool/Buffer metadata ABI differs')
    used, backing_count = 0, 0
    raw_files_start = len(snapshot.files)

    def read(address, size, name):
        nonlocal used
        require(0 < size <= MAX_BYTES - used, '1MiB NVN metadata budget exceeded')
        used += size
        return snapshot.read(address, size, 'nvn-' + name)

    def typed(address, typ, name):
        raw = read(address, typ.GetByteSize(), name + '.bin')
        data, error = lldb.SBData(), lldb.SBError()
        data.SetData(error, raw, lldb.eByteOrderLittle, 8)
        require(error.Success(), 'official SBData conversion failed')
        value = target.CreateValueFromData(name, data, typ)
        require(value.IsValid() and value.GetError().Success(), 'official local typed metadata failed')
        return value

    def scalar(name, phase):
        v = values[name]
        require(v.GetType().GetByteSize() == 8, 'actual registry scalar/pointer width differs')
        raw = read(v.GetLoadAddress(), 8, name + '-' + phase + '.bin')
        return int.from_bytes(raw, 'little')

    before = read(mutex.GetLoadAddress(), 40, 'resources-before.bin')
    normal_resources_unlocked(before, pin['mode'])
    head, epoch, adds = [scalar(n, 'before') for n in ['pools', 'pool_registry_epoch', 'pool_adds']]
    result, seen_pools, seen_backings = [], set(), set()
    address = head
    while address:
        snapshot.check()
        require(address not in seen_pools and len(seen_pools) < MAX_POOLS and address % 8 == 0, 'Pool cycle/address/budget')
        seen_pools.add(address)
        p = typed(address, ptype, 'pool-' + format(address, 'x'))
        row = dict(metadata=address, **fields(p, ['next', 'guest', 'size', 'identity', 'backing_generation',
                    'shared', 'shared_closing', 'shared_lazy_eligible', 'shared_import_bytes',
                    'shared_unique_bytes', 'shared_import_nodes', 'shared_cover_nodes', 'shared_properties',
                    'nvn_flags', 'nvn_flags_known', 'native_allocations']))
        row['in_captured_heap'] = snapshot.config['_sdk_heap_begin'] <= row['guest'] and row['guest'] + row['size'] <= snapshot.config['_sdk_heap_end']
        for chain in ['backings', 'shared_covers']:
            row[chain] = []
            baddr = integer(member(p, chain))
            while baddr:
                require(baddr not in seen_backings and backing_count < MAX_BACKINGS and baddr % 8 == 0, 'Buffer chain cycle/address/budget')
                seen_backings.add(baddr)
                backing_count += 1
                b = typed(baddr, btype, 'buffer-' + format(baddr, 'x'))
                item = dict(metadata=baddr, **fields(b, ['next', 'begin', 'end', 'generation']))
                item['buffer'] = fields(member(b, 'buffer'), ['handle', 'memory', 'allocation', 'map', 'size', 'properties', 'shared_import'])
                row[chain].append(item)
                baddr = integer(member(b, 'next'))
        result.append(row)
        address = integer(member(p, 'next'))
    after_head, after_epoch, after_adds = [scalar(n, 'after') for n in ['pools', 'pool_registry_epoch', 'pool_adds']]
    after = read(mutex.GetLoadAddress(), 40, 'resources-after.bin')
    normal_resources_unlocked(after, pin['mode'])
    require(before == after, 'resources mutex changed at the same all-stop')
    summary = validate_registry(result, head, epoch, adds, after_head, after_epoch, after_adds)
    descriptor = dict(complete=False, scope='capacity scalars/type sizes only; no descriptor-array traversal')
    try:
        for name in ['texture_capacity', 'sampler_capacity']:
            v = unique_global(target, name, 'nvn_vk_texture.c')
            require(v.GetType().GetByteSize() == 4, 'descriptor capacity scalar ABI differs')
            raw = read(v.GetLoadAddress(), 4, name + '.bin')
            descriptor[name] = int.from_bytes(raw, 'little', signed=True)
            require(0 <= descriptor[name] <= 1048576, 'descriptor capacity outside f75 cap')
        for name in ['texture_descriptors', 'sampler_descriptors']:
            v = unique_global(target, name, 'nvn_vk_texture.c')
            descriptor[name + '_element_bytes'] = v.GetType().GetPointeeType().GetByteSize()
        descriptor['complete'] = True
    except Exception as error:
        descriptor['error'] = repr(error)
    snapshot.check()
    return dict(complete=True, status='SAME_SDK_STOP_NVN_POOL_REGISTRY_METADATA_COMPLETE',
                inferior_calls=False, mutex_mode=pin['mode'], libc_sha256=pin.get('libc_sha256'), resources_mutex_address=mutex.GetLoadAddress(),
                resources_before_hex=before.hex(), resources_after_hex=after.hex(),
                namespace_globals=declarations, actual_Pool_bytes=ptype.GetByteSize(),
                actual_Buffer_node_bytes=btype.GetByteSize(), registry_head=head,
                registry_epoch=epoch, pool_adds=adds, pools=result, summary=summary,
                descriptor_metadata=descriptor, metadata_acquisition_bytes=used,
                raw_files=snapshot.files[raw_files_start:],
                scope='Complete current logical Pool list and canonical/shared-cover Buffer chains only. Native VkImage allocation chains are not traversed. Imported intervals can overlap and are not physical RAM/PSS.',
                limitations=['The SDK allocator mutex does not protect NVN; a V.resources representation not qualified by the selected pinned ABI rejects this optional section without following its pointers.',
                             'ABI-qualified normal/private mutex+one all-stop and head/epoch recheck qualify stable CPU registry metadata only; not GPU completion or contents.',
                             'The current registered list excludes removed Pools that may still be draining references; absence is not proof of no retained GPU owner.',
                             'Only logical Pool address ownership is established. Buffer interval sizes are not physical memory residency or unique committed GPU allocation bytes.'])

# Three original-main fixed records, qualified only for the reviewed930 bundle.
# This is bounded provenance metadata, not a TLSF walker or allocation tracker.
ARENA_MODE = 'original-main-db0507-fixed-three-arenas-930-v1'
MAIN_SHA = '55fd909bb7230b002357e5b0d8499c11f06888f345c49e69eb4734df44a27d0e'
MAIN_FLAT_SHA = '6843ab6909f96e1558b4b73b0fa807f3873409120cf3e1ef8a86dbebb275ee27'
MAIN_REWRITTEN_SHA = '89469c8694183b617464f7a08e5245f60246fb7fca2fb1d2409b2ecf63947cd7'
CODE_PROOFS = (
    (0x1348c8, 52, '77e2f8edce65c346476cb0347d3fa3654ab990a5bb399966d8216d16c316965c'),
    (0xe01c14, 136, 'e32f9916ec58fd9ee2223e716fc13660da1633d680324ab653dea83a9a6a991c'),
    (0x12952e0, 88, 'cfe21474029b1693470825ae84cef2513c723bb2f0fe277f884d7225defc4640'),
    (0x1295378, 108, '3778adbb4c60f5863d12ef4bf361bdfd7a30942ca1350bf0925a6bb7e54441f9'),
    (0x1295428, 32, '262b69149eee554bcad67b03cc10f65f0bc2a003a23cad64fe6fbf3a85cff684'),
    (0x1b63520, 64, '7742c8f7d51b2d43d11a7345debf69b5d819ff466e0c0f9452582af61dc1067e'),
)
STRING_PROOFS = (
    (0x1d01ae2, b'Popcorn Asset Pool\0'),
    (0x1d611b8, b'Popcorn Scratch Pool\0'),
    (0x1d205a3, b'tlsf_create: Memory must be aligned to %u bytes.\n\0'),
)
ARENA_RECORDS = (
    ('unlabelled_250MiB_backing', 0x31162e0, 0xfa00000, None, None),
    ('Popcorn Asset Pool', 0x31dafa8, 0x10400000, 0x31daff8, 0x1d01ae2),
    ('Popcorn Scratch Pool', 0x31db008, 0x2800000, 0x31db058, 0x1d611b8),
)


def validate_main_arena_pin(pin):
    require(isinstance(pin, dict) and set(pin) ==
            {'enabled', 'mode', 'main_original', 'main_flat', 'qualified_main', 'raw_ready'},
            'unknown/missing main_arena_origins config')
    require(pin['enabled'] is True and pin['mode'] == ARENA_MODE, 'unqualified main arena mode')
    expected = dict(main_original=MAIN_SHA, main_flat=MAIN_FLAT_SHA, qualified_main=MAIN_REWRITTEN_SHA)
    for key in ('main_original', 'main_flat', 'qualified_main', 'raw_ready'):
        row = pin[key]
        require(isinstance(row, dict) and set(row) == {'path', 'sha256'}, 'bad arena file pin ' + key)
        require(isinstance(row['sha256'], str) and len(row['sha256']) == 64 and
                all(c in '0123456789abcdef' for c in row['sha256']), 'bad arena digest ' + key)
        require(hashlib.sha256(Path(row['path']).read_bytes()).hexdigest() == row['sha256'],
                'arena local file hash changed ' + key)
        if key in expected:
            require(row['sha256'] == expected[key], 'arena original/qualified identity differs ' + key)
    ready = json.loads(Path(pin['raw_ready']['path']).read_text())
    require(ready['status'] == 'FRESH_ORIGINAL_NSO_REBUILD_AND_SEPARATE_VERIFY_ONLY_PASS_NO_DEVICE' and
            ready['source_commit'] == '930ac8fd367007c35261eb0e1c398445087a25f6' and
            ready['raw_original_inputs']['main']['sha256'] == MAIN_SHA and
            ready['28_deployment_file_hashes']['main.patched.bin'] == MAIN_REWRITTEN_SHA,
            'arena raw producer/verify-only qualification differs')
    original = Path(pin['main_flat']['path']).read_bytes()
    rewritten = Path(pin['qualified_main']['path']).read_bytes()
    for off, length, sha in CODE_PROOFS:
        require(hashlib.sha256(original[off:off + length]).hexdigest() == sha and
                original[off:off + length] == rewritten[off:off + length], 'arena original instruction proof differs')
    for off, text in STRING_PROOFS:
        require(original[off:off + len(text)] == rewritten[off:off + len(text)] == text,
                'arena original string proof differs')


def arena_record_attribution(raw, spec, main_base, primary, alias, current, spans,
                             host_page, primary_pte, alias_pte, pools, check=lambda: None):
    """Correlate saved metadata with this snapshot only; no extra target reads."""
    label, record_rva, expected_bytes, label_rva, string_rva = spec
    require(len(raw) == 32, 'arena short fixed record')
    owner, pointer, size, control = struct.unpack('<QQQQ', raw)
    require(owner > 0 and pointer > 0 and control > 0 and size == expected_bytes,
            'arena empty/mid-initialization or size differs')
    require(pointer % 16 == 0 and pointer + size < (1 << 64) and pointer <= control < pointer + size,
            'arena pointer/control bounds differ')
    views = [('primary', primary), ('import_alias', alias)]
    matches = [(name, pointer - base) for name, base in views
               if base <= pointer and pointer + size <= base + current]
    require(len(matches) == 1, 'arena pointer outside/ambiguous captured heap aliases')
    view, offset = matches[0]
    begin, end = primary + offset, primary + offset + size
    allocated = [s for s in spans if s['state'] == 1 and s['begin'] <= begin and end <= s['end']]
    require(len(allocated) == 1, 'arena has no unique containing current allocated SDK span')
    require(host_page >= 4096 and host_page & (host_page - 1) == 0 and
            current % host_page == 0 and len(primary_pte) == len(alias_pte) == current // host_page * 8,
            'arena saved pagemap geometry differs')
    present = dict(primary_bytes=0, import_alias_bytes=0, union_bytes=0,
                   primary_swap_flag_bytes=0, import_alias_swap_flag_bytes=0)
    # Count only the overlap bytes of each page, never duplicate alias backing.
    for idx in range(offset // host_page, (offset + size + host_page - 1) // host_page):
        if idx % 4096 == 0:
            check()
        overlap = min(offset + size, (idx + 1) * host_page) - max(offset, idx * host_page)
        p, a = struct.unpack_from('<Q', primary_pte, idx * 8)[0], struct.unpack_from('<Q', alias_pte, idx * 8)[0]
        if p & (1 << 63): present['primary_bytes'] += overlap
        if a & (1 << 63): present['import_alias_bytes'] += overlap
        if (p | a) & (1 << 63): present['union_bytes'] += overlap
        if p & (1 << 62): present['primary_swap_flag_bytes'] += overlap
        if a & (1 << 62): present['import_alias_swap_flag_bytes'] += overlap
    intersections = []
    for pool in pools:
        pbegin, pend = pool['guest'], pool['guest'] + pool['size']
        pm = [(pbegin - base, pend - base) for _, base in views if base <= pbegin < pend <= base + current]
        require(len(pm) <= 1, 'Pool alias interval ambiguous')
        if pm:
            lo, hi = max(offset, pm[0][0]), min(offset + size, pm[0][1])
            if lo < hi:
                intersections.append(dict(identity=pool['identity'], backing_begin=lo, backing_end=hi, bytes=hi-lo))
    return dict(name=label, record_RVA=record_rva, record_address=main_base + record_rva,
                owner=owner, pointer=pointer, bytes=size, control=control,
                source_alias=view, backing_offset=offset, primary_begin=begin, primary_end=end,
                containing_SDK_span=allocated[0], present_cpu_views=present,
                current_registered_Pool_intersections=intersections,
                scope='Static record correlated with one current SDK allocated span and same-stop CPU PTE views; not TLSF live-block state, unique physical RAM/PSS or reclaim permission')


def capture_main_arenas(target, snapshot, pin, main, heap, spans, host_page, pp, ap, nvn):
    result = dict(complete=False, inferior_calls=False, status='SAME_SDK_STOP_MAIN_ARENA_ORIGINS_UNAVAILABLE',
                  errors=[], records=[], raw_files=[])
    start, used = len(snapshot.files), 0

    def read(off, length, name):
        nonlocal used
        require(0 <= off < off + length <= main['bytes'], 'arena read outside actual main module')
        require(length > 0 and used + length <= 850 and used + length <= 4096, 'arena independent metadata budget exceeded')
        used += length
        return snapshot.read(main['base'] + off, length, 'main-arena-' + name)

    try:
        validate_main_arena_pin(pin)
        require(snapshot.config['elf']['sha256'] == ELF_930_SHA and
                snapshot.config['elf']['buildid'] == BUILD_930_ID, 'arena capture is not exact930 ELF')
        require(nvn.get('complete') is True and nvn.get('mutex_mode') == MODE_930_Q735,
                'arena ownership requires complete same-stop930 Pool metadata')
        require(nvn.get('libc_sha256') == Q735_LIBC_SHA, 'arena mutex ABI qualification absent')
        for off, length, sha in CODE_PROOFS:
            require(hashlib.sha256(read(off, length, 'code-' + format(off, 'x') + '.bin')).hexdigest() == sha,
                    'live main arena instruction proof differs')
        for off, text in STRING_PROOFS:
            require(read(off, len(text), 'string-' + format(off, 'x') + '.bin') == text,
                    'live main arena original string differs')
        before, labels = {}, {}
        for spec in ARENA_RECORDS:
            off, label = spec[1], spec[3]
            before[off] = read(off, 32, 'record-' + format(off, 'x') + '-before.bin')
            if label is not None:
                labels[label] = read(label, 8, 'label-' + format(label, 'x') + '-before.bin')
                require(int.from_bytes(labels[label], 'little') == main['base'] + spec[4], 'live arena label pointer differs')
        capacity = read(0x31164a0, 8, '250-capacity-before.bin')
        require(int.from_bytes(capacity, 'little') == 0xfa00000, 'live250 manager capacity differs')
        for spec in ARENA_RECORDS:
            result['records'].append(arena_record_attribution(before[spec[1]], spec, main['base'],
                heap['g_heap'], heap['g_heap_import_alias'], heap['g_heap_cur'], spans,
                host_page, pp, ap, nvn['pools'], snapshot.check))
        for spec in ARENA_RECORDS:
            off, label = spec[1], spec[3]
            require(read(off, 32, 'record-' + format(off, 'x') + '-after.bin') == before[off],
                    'arena fixed record changed at all-stop')
            if label is not None:
                require(read(label, 8, 'label-' + format(label, 'x') + '-after.bin') == labels[label],
                        'arena label pointer changed at all-stop')
        require(read(0x31164a0, 8, '250-capacity-after.bin') == capacity, 'arena manager capacity changed')
        snapshot.check()
        result.update(complete=True, status='SAME_SDK_STOP_MAIN_ARENA_ORIGINS_COMPLETE', same_debugger_stop=True)
    except Exception as error:
        result['errors'].append(repr(error))
    result.update(metadata_acquisition_bytes=used, raw_files=snapshot.files[start:],
                  independent_byte_limit=4096, fixed_read_plan_bytes=810,
                  limitations=['Nonpresent CPU PTE is not proof of absent resident physical memory.',
                               'No TLSF block metadata or payload is traversed; allocated SDK arena capacity is not actual game-object usage.',
                               'All-stop and reread establish saved metadata consistency, not an independent arena lifetime/reclamation contract.'])
    return result
