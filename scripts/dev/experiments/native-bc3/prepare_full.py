#!/usr/bin/env python3
"""Prepare the exact current native selection from original BC3 CASC; no device."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[4]
PRODUCER = '9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa'
EXPECTED = 6217
TOOLS = [
    'scripts/dev/prepare-native-image-resource.py',
    'scripts/dev/prepare-native-image-batch.py',
    'scripts/dev/native_static_selection.py',
    'scripts/dev/native-image-payload/resource_format.h',
    'dm/tools/astc_repack.py', 'dm/tools/casc_overlay.py',
    'dm/ios/tools/romfs_art.py', 'dm/ios/tools/touch_layout.py',
    'dm/ios/tools/gen_paths.py',
    'dm/gfx/nvn/nvn_tex.c', 'dm/gfx/nvn/nvn_tex.h',
    'dm/gfx/tex/nvn_deswizzle.c', 'dm/gfx/tex/nvn_deswizzle.h',
]


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def record(path):
    path = Path(path).resolve()
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path)}


def require(value, message):
    if not value:
        raise ValueError(message)


def save(path, data):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
    temporary.replace(path)


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def producer_gate(path):
    ready_path = path / 'source-ready.json'
    ready = json.loads(ready_path.read_text())
    require(ready['complete'] and ready['producer_source_sha256'] == PRODUCER and
            ready['profile'] == 'turnip-coherent-host-bc3-npot-reference-v2',
            'producer v2 identity mismatch')
    inputs = ready['CMake_inputs']
    folder = path / 'source/scripts/dev/native-image-payload'
    for name, digest in inputs.items():
        require(sha(folder / name) == digest, 'producer CMake source changed: ' + name)
    content = ''.join(k + ':' + v + '\n' for k, v in inputs.items())
    content += 'contract-profile:' + ready['profile'] + '\n'
    require(hashlib.sha256(content.encode()).hexdigest() == PRODUCER,
            'producer CMake identity does not reproduce')
    return record(ready_path)


def run(a):
    require(not a.output.exists(), 'new output only; never resume partial completion')
    producer_ready = producer_gate(a.producer)
    selection_record = record(a.selection)
    old = json.loads(a.selection.read_text())
    require(old['complete'] and len(old['entries']) == EXPECTED,
            'selection must be the complete 6217 current native entries')
    names = [r['logical'] for r in old['entries']]
    require(len(set(names)) == EXPECTED and
            all(hashlib.sha256(r['logical'].encode()).hexdigest() == r['id']
                for r in old['entries']), 'duplicate or invalid logical selection')
    output = a.output.resolve()
    output.mkdir(parents=True)
    (output / 'items').mkdir()
    (output / 'batches').mkdir()
    proof = output / 'source-context'
    frozen = proof / 'source-tools'
    fingerprints = {}
    for relative in TOOLS:
        source = ROOT / relative
        target = frozen / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        before = sha(source)
        shutil.copy2(source, target)
        require(before == sha(target) == sha(source), 'tool changed during snapshot')
        fingerprints[relative] = record(target)
    shutil.copy2(a.selection, proof / 'selection.json')
    shutil.copy2(a.producer / 'source-ready.json', proof / 'producer-source-ready.json')
    shutil.copy2(Path(__file__), proof / 'prepare_full.py')
    sys.path.insert(0, str(frozen / 'scripts/dev'))
    resource = load_module('original_full_resource', frozen / TOOLS[0])
    batch = load_module('original_full_batch', frozen / TOOLS[1])
    args = SimpleNamespace(romfs=a.romfs.resolve(), pack_index=None, data_dir=None,
                           pack_manifest=[], composition_report=None,
                           producer_sha256=PRODUCER, original_bc3=True)
    context = resource.OriginalBC3Context(args)
    original_lib = Path(context.common['NVN_layout_library']['path'])
    shutil.copy2(original_lib, proof / 'nvn-deswizzle.so')
    context.common['NVN_layout_library'] = record(proof / 'nvn-deswizzle.so')
    manifest = {
        'schema': 1, 'complete': False,
        'contract': 'DM native resource batch preparation v1',
        'source_context': context.common,
        'producer_source_sha256': PRODUCER, 'producer_source_ready': producer_ready,
        'selection': {'kind': 'exact-current-pack-entry-order', 'count': EXPECTED,
                      'prior_selection': selection_record,
                      'same_complete_logical_names_and_order': True,
                      'source_profile': 'original-bc3-raw-casc-v1'},
        'batch_max_entries': batch.MAX_BATCH_ENTRIES, 'entries': [], 'batches': [],
        'tool': record(__file__), 'frozen_tools': fingerprints,
        'encoder_used': False, 'device_or_game_executed': False,
        'native_layout_query_pending': True,
    }
    save(output / 'batch-prepare.incomplete.json', manifest)
    formats, dimensions, levels, families, thumbnails = (Counter() for _ in range(5))
    totals = Counter()
    problems = []
    original = resource.casc.read_manifest(args.romfs)
    census_rows = []
    for name in names:
        entry = context.entries.get(name)
        if entry is None:
            problems.append({'logical': name, 'reason': 'original descriptor missing'})
            continue
        try:
            nvn, vk, block = resource.BC3_FORMATS[entry['format']]
            desc = resource.repack.desc(entry, nvn, {entry['format']})
            offsets = resource.repack.geometry(desc)
            require(0 < desc.levels <= 16, 'mip count outside producer contract')
            compact = sum(resource.repack.block_bytes(*resource.repack.dimensions(
                desc.width, desc.height, l), block) for l in range(desc.levels))
            require(offsets[-1] <= resource.MAX_BYTES and 1088 + compact <= resource.MAX_BYTES,
                    'source/native input exceeds existing 16MiB limit')
            thumb = bool(entry.get('thumbnailBase64'))
            require(not thumb or 0 < entry['thumbnailMip'] < entry['levels'],
                    'thumbnail mip cannot distinguish its complete tail')
            row = original[name]
            family = ('act1-environment' if name.startswith('data/hd/env/') else
                      'character' if name.startswith('data/hd/character/') else
                      'character-vfx' if name.startswith('data/hd/vfx/meshes/character/') else
                      'other')
            formats[str(vk)] += 1
            dimensions[f'{desc.width}x{desc.height}'] += 1
            levels[str(desc.levels)] += 1
            families[family] += 1
            thumbnails['present' if thumb else 'absent'] += 1
            totals.update(source_texture_decoded_bytes=int(row[5]),
                          source_texture_encoded_bytes=int(row[7]),
                          linear_compressed_bytes=compact,
                          wire_input_bytes=1088 + compact,
                          original_NVN_padded_bytes=offsets[-1])
            census_rows.append({'logical': name, 'VkFormat': vk, 'width': desc.width,
                                'height': desc.height, 'levels': desc.levels,
                                'thumbnail': thumb, 'thumbnailMip': entry.get('thumbnailMip'),
                                'family': family, 'linear_bytes': compact,
                                'source_decoded_bytes': int(row[5])})
        except (KeyError, TypeError, ValueError) as exc:
            problems.append({'logical': name, 'reason': str(exc)})
    census = {'complete_selection_examined': len(names), 'supported_entries': len(census_rows),
              'all_supported': not problems, 'problems': problems, 'VkFormats': formats,
              'dimensions': dimensions, 'mip_counts': levels, 'families': families,
              'thumbnail_counts': thumbnails, 'totals': totals,
              'source_data_integrity_and_roundtrip_pending_until_prepared': True,
              'entries': census_rows, 'producer_source_sha256': PRODUCER,
              'selection': selection_record}
    save(output / 'census.json', census)
    print(json.dumps({'stage': 'census', **{k: v for k, v in census.items()
                     if k not in ['entries', 'dimensions', 'selection']}}), flush=True)
    require(not problems, 'full selection contains unsupported inputs; no subset prepared')
    specs = []
    with (output / 'prepare-entries.jsonl').open('x') as checkpoint:
        for ordinal, name in enumerate(names):
            identity = resource.sha(name.encode())
            folder = output / 'items' / identity
            args.logical, args.output = name, folder
            try:
                result = resource.prepare(args, context)
            except Exception as exc:
                save(output / 'failure.json', {'ordinal': ordinal, 'logical': name,
                     'completed_entries': len(manifest['entries']), 'error': str(exc),
                     'complete': False, 'no_subset_completion': True})
                raise
            row = {'id': identity, 'logical': name,
                   'source': resource.record(folder / 'resource-source.json'),
                   'texture': resource.record(folder / 'source.texture'),
                   'input': result['input'],
                   'prepare_report': resource.record(folder / 'prepare-report.json')}
            manifest['entries'].append(row)
            specs.append({'source': row['source']['path'], 'texture': row['texture']['path'],
                          'baked': str(folder)})
            checkpoint.write(json.dumps(row, sort_keys=True) + '\n')
            checkpoint.flush()
            if len(manifest['entries']) % batch.MAX_BATCH_ENTRIES == 0:
                print(json.dumps({'stage': 'prepare', 'complete_entries': ordinal + 1,
                                  'expected_entries': EXPECTED}), flush=True)
        for number, start in enumerate(range(0, EXPECTED, batch.MAX_BATCH_ENTRIES)):
            entries = manifest['entries'][start:start + batch.MAX_BATCH_ENTRIES]
            path = output / 'batches' / f'{number:04d}.tsv'
            path.write_bytes(batch.batch_text(entries))
            manifest['batches'].append({'ordinal': number, 'manifest': record(path),
                                        'entry_ids': [r['id'] for r in entries]})
    context.validate_sources()
    require(record(a.selection) == selection_record, 'selection changed during preparation')
    require(producer_gate(a.producer) == producer_ready, 'producer changed during preparation')
    require(all(sha(Path(r['path'])) == r['sha256'] for r in fingerprints.values()),
            'frozen source tool changed during preparation')
    require([r['logical'] for r in manifest['entries']] == names,
            'prepared selection differs in names/order')
    manifest.update(complete=True, totals={
        'entries': EXPECTED, 'input_bytes': sum(r['input']['bytes'] for r in manifest['entries']),
        'source_texture_bytes': sum(r['texture']['bytes'] for r in manifest['entries']),
        'max_input_bytes': max(r['input']['bytes'] for r in manifest['entries']),
        'native_payload_total_bytes': None, 'native_layout_query_pending': True})
    save(output / 'entries.json', specs)
    save(output / 'batch-prepare.json', manifest)
    print(json.dumps({'stage': 'complete', 'entries': EXPECTED,
                      'batches': len(manifest['batches']),
                      'manifest': record(output / 'batch-prepare.json')}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--romfs', type=Path, required=True)
    p.add_argument('--producer', type=Path, required=True)
    p.add_argument('--selection', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    run(p.parse_args())
