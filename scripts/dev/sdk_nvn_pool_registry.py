"""Optional f75 NVN Pool metadata at the existing SDK allocator all-stop.
Official LLDB typed values over saved read-only bytes, no target calls/locks.
The owning sdk_free_resident Snapshot supplies the unchanged60s/40MiB limits.
"""
import hashlib
from pathlib import Path

MODE = 'f75-bionic-normal-resources-zero-v1'
MODE_Q735 = 'f75-q735-libc-02969f76-state16-v2'
Q735_LIBC_SHA = '02969f76be8e6ed8a44be78083735801448a6e0f972792da63b93e4a6de8d5b0'
ELF_SHA = '4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618'
BUILD_ID = '6e6f12c5c5f7a858704a84ee294b19d95aa3f894'
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
    if mode == MODE_Q735:
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
    require(pin['mode'] in (MODE, MODE_Q735), 'NVN metadata mode not qualified')
    if pin['mode'] == MODE_Q735:
        require(pin.get('libc_sha256') == Q735_LIBC_SHA, 'actual Q735 libc ABI pin missing/differs')
    else:
        require('libc_sha256' not in pin, 'old mutex mode must not imply another libc ABI')
    require(pin['ELF_sha256'] == ELF_SHA == config['elf']['sha256'], 'NVN metadata source/ELF hash not f75')
    require(pin['ELF_BuildID'].replace('-', '').lower() == BUILD_ID == config['elf']['buildid'].replace('-', '').lower(), 'NVN metadata source/BuildID not f75')
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
