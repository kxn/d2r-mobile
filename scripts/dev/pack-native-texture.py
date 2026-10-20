#!/usr/bin/env python3
"""Build DMN1 containers/table from verified producer outputs; never deploy.

The mip table describes native physical spans including padding, not compact
ASTC blocks. Thumbnails remain the original linear bytes. This does not prove
that a game's thumbnail builder uses a different layout: that is a separate
runtime integration gate, explicitly retained in the output manifest.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def sha(data):
    return hashlib.sha256(data).digest()


def canonical(obj):
    return json.dumps(obj, sort_keys=True, separators=(',', ':')).encode()


def load_entry(spec, number):
    source_path = Path(spec['source'])
    source_bytes = source_path.read_bytes()
    source = json.loads(source_bytes)
    linear_input = source_path.with_name('resource-input.bin')
    original = Path(spec['texture']).read_bytes()
    baked = Path(spec['baked'])
    wire = (baked / 'native-resource-layout.bin').read_bytes()
    payload = (baked / 'native-resource.bin').read_bytes()
    metadata = (baked / 'native-resource.json').read_bytes()
    if len(wire) != 992 or wire[:8] != b'DMNRES1\0':
        raise ValueError('unknown native producer ABI')
    if sha(wire[:960] + bytes(32)) != wire[960:]:
        raise ValueError('native recipe digest mismatch')
    version, header, size, alignment = struct.unpack_from('<IIQQ', wire, 8)
    recipe = struct.unpack_from('<20I', wire, 32)
    fmt, width, height, depth, levels, layers, kind, samples, tiling, usage, flags, sharing, initial, handle, layout, family, *_ = recipe
    descriptor = source['source_descriptor']
    format_map = {157: (95, 0x79), 158: (81, 0x87), 165: (99, 0x7d), 166: (85, 0x8b)}
    if (fmt not in format_map or (version, header) != (1, 992) or
            size != len(payload) or not 0 < size <= 16 << 20 or
            alignment < 4096 or alignment > 65536 or alignment & (alignment - 1) or
            (depth, layers, kind, samples, tiling, flags, sharing, initial, handle, layout, family) !=
            (1, 1, 1, 1, 0, 0, 0, 0, 128, 1, 0xfffffffd) or
            usage & ~0x400007 or usage & 0x400004 != 0x400004 or
            not 1 <= levels <= min(16, max(width, height).bit_length())):
        raise ValueError('unsupported native resource recipe')
    game, nvn = format_map[fmt]
    if ((descriptor['format'], descriptor['width'], descriptor['height'], descriptor['levels'],
         descriptor['type'], descriptor['depth'], descriptor['layoutType']) !=
            (game, width, height, levels, 2, 1, 3) or
            sha(original).hex() != source['source_texture']['sha256'] or
            sha(source_bytes) != wire[224:256] or sha(linear_input.read_bytes()) != wire[160:192] or
            sha(metadata) != wire[256:288] or
            sha(payload) != wire[192:224]):
        raise ValueError('source/container/producer declaration mismatch')
    # Check all old header fields before changing only its packaged layout.
    if (len(original) < 36 or original[:4] != b'<DE(' or original[4] != game or
            original[6] != 2 or struct.unpack_from('<II', original, 8) != (width, height) or
            struct.unpack_from('<H', original, 16)[0] != 1 or original[18] != 3 or
            struct.unpack_from('<Q', original, 19)[0] != descriptor['layoutData'] or
            struct.unpack_from('<I', original, 28)[0] != levels):
        raise ValueError('original full texture header disagrees with descriptor')
    tab = 32 + struct.unpack_from('<I', original, 32)[0]
    first = tab + 8 * levels
    if tab < 36 or first > len(original):
        raise ValueError('original mip table bounds')
    cursor = first
    for n in range(levels):
        count, relative = struct.unpack_from('<Ii', original, tab + 8 * n)
        if not count or tab + 8 * n + 4 + relative != cursor or count > len(original) - cursor:
            raise ValueError('original mip table is not a complete contiguous payload')
        cursor += count
    if cursor != len(original):
        raise ValueError('original texture trailing payload')
    mips = [struct.unpack_from('<5Q', wire, 320 + n * 40) for n in range(16)]
    end = 0
    for n, mip in enumerate(mips):
        offset, count, *_ = mip
        if n >= levels:
            if any(mip):
                raise ValueError('nonzero unused native mip')
        elif (not count or (not n and offset) or offset < end or
              offset > size or count > size - offset):
            raise ValueError('native mip physical bounds')
        else:
            end = offset + count
    if descriptor.get('thumbnailBase64') and descriptor.get('thumbnailMip') in (None, 0):
        raise ValueError('thumbnail has the full Builder shape: source encoding cannot be distinguished by this family protocol')
    marker = b'DMN\2' + struct.pack('<I', number)
    desc = dict(descriptor, layoutData=int.from_bytes(marker, 'little'))
    result = bytearray(original[:first])
    struct.pack_into('<Q', result, 19, desc['layoutData'])
    for n in range(levels):
        start = mips[n][0]
        stop = mips[n + 1][0] if n + 1 < levels else size
        if stop <= start or stop - start > 0xffffffff:
            raise ValueError('native serialized mip extent')
        struct.pack_into('<Ii', result, tab + 8 * n, stop - start, first + start - (tab + 8 * n + 4))
    result.extend(payload)
    entry = struct.pack('<II4i5Ii', number, 0, 1, nvn, 0, 1, width, height, 1, levels, 0, 0) + wire
    assert len(entry) == 1040
    logical = descriptor['filename']
    if not isinstance(logical, str) or Path(logical).is_absolute() or '..' in Path(logical).parts or any(c in logical for c in '\\\t\r\n'):
        raise ValueError('unsafe resource logical path')
    detail = {'id': number, 'logical': logical, 'source_texture_sha256': sha(original).hex(),
              'source_declaration_sha256': sha(source_bytes).hex(), 'native_recipe_sha256': sha(wire).hex(),
              'native_payload_sha256': sha(payload).hex(), 'texture_sha256': sha(result).hex(),
              'descriptor_sha256': sha(canonical(desc)).hex(), 'layout8': marker.hex(),
              'image_size': size, 'alignment': alignment, 'usage': usage,
              'native_mip_offsets': [mips[n][0] for n in range(levels)],
              'thumbnail_bytes_preserved': desc.get('thumbnailBase64') == descriptor.get('thumbnailBase64'),
              'original_layout_data': descriptor['layoutData'],
              'thumbnail_mip': descriptor.get('thumbnailMip')}
    return entry, result, desc, detail, wire[112:160]


def queried_identity(path):
    # Existing actual producer JSON: UUIDs are independent properties, never
    # inferred from a driver ELF BuildID. Outer device tooling still authenticates
    # the selected loaded driver against the complete driver build record.
    record = json.loads(path.read_bytes())
    if (record.get('contract') != 'DM native image producer v1' or
            record.get('complete') is not True or record.get('driverID') != 18):
        raise ValueError('Turnip identity requires complete actual producer query metadata')
    result = bytearray()
    for key in ('layout_UUID', 'device_UUID', 'driver_UUID'):
        value = record.get(key)
        if not isinstance(value, str) or len(value) != 32 or any(c not in '0123456789abcdef' for c in value):
            raise ValueError('invalid queried native identity: ' + key)
        part = bytes.fromhex(value)
        if not any(part):
            raise ValueError('zero queried native identity: ' + key)
        result.extend(part)
    producer = record.get('producer_source_sha256')
    if not isinstance(producer, str) or len(producer) != 64 or any(c not in '0123456789abcdef' for c in producer):
        raise ValueError('invalid queried producer source identity')
    return bytes(result), bytes.fromhex(producer)


def build(specifications, output, driver_manifest, base_cache=None, driver_query=None):
    driver_record = json.loads(driver_manifest.read_text())
    driver = Path(driver_record['driver']['path'])
    driver_sha = sha(driver.read_bytes())
    if driver_sha.hex() != driver_record['driver']['sha256']:
        raise ValueError('driver binary differs from its frozen build record')
    if not isinstance(specifications, list) or not 1 <= len(specifications) <= 65536:
        raise ValueError('empty/oversized pack')
    # One payload at a time. Keep only bounded per-entry metadata, not a second
    # in-memory copy of the entire texture pack. No complete manifest is
    # published if a later entry fails; existing/partial directories reject.
    cache_bytes = base_cache.read_bytes() if base_cache else None
    old_cache = json.loads(cache_bytes) if cache_bytes is not None else None
    actual_identity, query_producer = queried_identity(driver_query) if driver_query else (None, None)
    if actual_identity is None:
        build_ids = driver_record.get('build_id', [])
        if len(build_ids) != 1 or len(build_ids[0]) < 32:
            raise ValueError('PanVK build record needs its real ELF build ID; Turnip requires actual --driver-query')
        layout_id = bytes.fromhex(build_ids[0])[:16]
    updates = {}
    output.mkdir(parents=True, exist_ok=False)
    loaded = []
    logicals = set()
    for n, specification in enumerate(specifications):
        entry, texture, descriptor, detail, identity = load_entry(specification, n + 1)
        # This PanVK contract copies the ELF build ID into both UUIDs. Keep
        # the independent full binary SHA binding as well. Never re-label an
        # old payload with a different driver build record.
        if actual_identity is not None:
            if identity != actual_identity:
                raise ValueError('baked native identities differ from actual Turnip producer query')
        elif identity[:16] != layout_id or identity[32:48] != layout_id:
            raise ValueError('baked native layout/driver UUID differs from selected PanVK ELF')
        if detail['logical'] in logicals:
            raise ValueError('duplicate logical texture')
        logicals.add(detail['logical'])
        if loaded and identity != loaded[0][4]:
            raise ValueError('mixed driver/layout identity')
        path = output / detail['logical']
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(texture)
        path.with_suffix('.descriptor.json').write_text(json.dumps(descriptor, sort_keys=True, indent=2) + '\n')
        updates[detail['logical']] = descriptor
        loaded.append((entry, None, None, detail, identity))
    details = [x[3] for x in loaded]
    families = []
    for detail in details:
        original_layout = detail['original_layout_data']
        thumbnail = detail['thumbnail_mip']
        if thumbnail is None:
            thumbnail = 0
        if (not isinstance(original_layout, int) or not 0 <= original_layout < 1 << 64 or
                not isinstance(thumbnail, int) or not 0 <= thumbnail < struct.unpack_from('<I', loaded[len(families)][0], 48 + 48)[0]):
            raise ValueError('invalid original layout/explicit thumbnail mip')
        source_layout = struct.pack('<Q', original_layout)
        if source_layout[0] > 5 or source_layout[:3] == b'DMN':
            raise ValueError('original packaged layout is not a supported NVN source')
        families.append(source_layout + struct.pack('<II', thumbnail, 0))
    producers = {x[0][48 + 288:48 + 320] for x in loaded}
    if len(producers) != 1:
        raise ValueError('mixed producer source fingerprints')
    producer_sha = producers.pop()
    if query_producer is not None and producer_sha != query_producer:
        raise ValueError('baked payload producer differs from actual Turnip query source')
    pack_sha = sha(canonical({'entries': details, 'driver_sha256': driver_sha.hex(), 'producer_source_sha256': producer_sha.hex()}))
    table = bytearray(struct.pack('<8s4IQ32s32s32s32s', b'DMNTBL2\0', 2, 160, len(loaded), 1040,
                                  160 + len(loaded) * 1056, pack_sha, driver_sha, producer_sha, bytes(32)))
    table.extend(b''.join(x[0] for x in loaded))
    table.extend(b''.join(families))
    table[128:160] = sha(table)
    replacements = [(x[3]['logical'], output / x[3]['logical']) for x in loaded]
    cache_record = None
    if old_cache is not None:
        if not isinstance(old_cache, list):
            raise ValueError('base descriptor cache is not a complete list')
        seen = set()
        merged = []
        for old in old_cache:
            logical = old['filename']
            if logical in seen:
                raise ValueError('duplicate base cache logical name')
            seen.add(logical)
            new = updates.get(logical)
            if new is not None:
                if {k: v for k, v in old.items() if k != 'layoutData'} != {k: v for k, v in new.items() if k != 'layoutData'}:
                    raise ValueError('native descriptor differs beyond its explicit layout marker')
                merged.append(new)
            else:
                merged.append(old)
        if not set(updates) <= seen:
            raise ValueError('selected resource absent from complete base cache')
        cache_name = 'data/hd/texture_desc_cache.json'
        cache_path = output / cache_name
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        cache_path.write_bytes(canonical(merged))
        replacements.append((cache_name, cache_path))
        cache_record = {'input_sha256': sha(cache_bytes).hex(),
                        'output_sha256': sha(cache_path.read_bytes()).hex(),
                        'entries': len(merged), 'selected': len(updates),
                        'unselected_entries_exact': True, 'all_thumbnail_bytes_exact': True}
    (output / 'replacements.tsv').write_text(''.join(f'{name}\t{path.resolve()}\n' for name, path in replacements))
    (output / 'native-layouts.bin').write_bytes(table)
    manifest = {'schema': 1, 'complete': True, 'runtime_default_enabled': False,
                'game_modules_executed': False, 'deployable_game_pack': False,
                'pack_content_sha256': pack_sha.hex(), 'table_sha256': sha(table).hex(),
                'identity': loaded[0][4].hex(), 'entries': details,
                'driver_binary_sha256': driver_sha.hex(), 'producer_source_sha256': producer_sha.hex(),
                'driver_build_record_sha256': sha(driver_manifest.read_bytes()).hex(),
                'actual_driver_query_sha256': sha(driver_query.read_bytes()).hex() if driver_query else None,
                'family_protocol': 2, 'tail_source_encoding': 'original-NVN-through-original-backend',
                'resident_table_bytes': len(table), 'complete_cache_merge': cache_record,
                'replacements_sha256': sha((output / 'replacements.tsv').read_bytes()).hex(),
                'pending_runtime_gates': ['original NVN full/native and tail/original GPU byte/sample regression',
                    'native writer scope and prewrite-gate ordinary UID composition',
                    'actual game full/thumbnail Builder storage and streaming acceptance'],
                'tool_sha256': sha(Path(__file__).read_bytes()).hex()}
    (output / 'native-pack.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--entries', type=Path, required=True, help='JSON list: source declaration, original texture, baked directory')
    parser.add_argument('--base-cache', type=Path, help='complete already-ASTC descriptor cache; merged once, unselected entries exact')
    parser.add_argument('--driver-manifest', type=Path, required=True)
    parser.add_argument('--driver-query', type=Path, help='actual Turnip native-resource.json from exact authenticated driver; preserves all three queried UUIDs')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(json.loads(args.entries.read_text()), args.output, args.driver_manifest, args.base_cache, args.driver_query), sort_keys=True))
