#!/usr/bin/env python3
"""Compose an already verified native texture pack through original CASC tools.

Offline only: no encoder, driver changes, game binary rewrite, ADB or deployment.
The same entry accepts linear and tiled native packs, bound to exact identities.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
helper_file = ROOT / 'scripts/dev/experiments/texture-format-parity/build.py'
spec = importlib.util.spec_from_file_location('original_overlay_helpers', helper_file)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
casc, composition, touch_layout = helpers.casc, helpers.composition, helpers.touch_layout
require, write, digest = helpers.require, helpers.write, helpers.digest
CACHE = helpers.CACHE


def cache_contract(original, updated, entries):
    """Native markers alone may change selected original descriptor rows."""
    before, after = helpers.descriptor_map(original), helpers.descriptor_map(updated)
    require([r['filename'] for r in original] == [r['filename'] for r in updated], 'cache order/names changed')
    selected, counts = {}, collections.Counter()
    for row in entries:
        logical = helpers.name(row['logical'].lower())
        require(logical in before and logical not in selected, 'duplicate/missing selection')
        require(row['id'] > 0 and row['id'] not in selected.values(), 'duplicate/invalid family id')
        old, new = original[before[logical]], updated[after[logical]]
        marker = bytes.fromhex(row['layout8'])
        require(len(marker) == 8 and marker[:4] == b'DMN\2' and int.from_bytes(marker[4:], 'little') == row['id'], 'native marker/id mismatch')
        require(old['format'] in (61, 62) and old['layoutData'] == row['original_layout_data'], 'not original BC3 source layout')
        require(new == dict(old, layoutData=int.from_bytes(marker, 'little')), 'native cache changes beyond family layout marker')
        selected[logical] = row['id']
        counts[str(old['format'])] += 1
    require(all(old == new for old, new in zip(original, updated) if old['filename'].lower() not in selected), 'unselected descriptor changed')
    return selected, dict(counts)


def build(config_path, native_path, output, driver_build, driver_query, producer_ready, workers, packing_receipt=None):
    require(not output.exists(), 'output must be new; preserve earlier or partial results')
    h = helpers.Inputs()
    for path in [config_path, native_path, Path(__file__), helper_file, Path(casc.__file__),
                 Path(composition.__file__), Path(touch_layout.__file__), ROOT / 'dm/ios/tools/romfs_art.py']:
        h.pin(path)
    cfg, native = json.loads(config_path.read_text()), json.loads(native_path.read_text())
    require(cfg['schema'] == 1 and native['complete'] is True and native['family_protocol'] == 2, 'unsupported/incomplete native pack')
    require(len(native['entries']) == cfg['expected_selected'], 'native selection count')
    pack_root = native_path.parent
    table, replacements = pack_root / 'native-layouts.bin', pack_root / 'replacements.tsv'
    h.pin(table, native['table_sha256']); h.pin(replacements, native['replacements_sha256'])
    d = json.loads(driver_build.read_text()); h.pin(driver_build, native['driver_build_record_sha256'])
    driver = Path(d['driver']['path']); h.pin(driver, native['driver_binary_sha256'])
    require(d['complete'] and d['driver']['sha256'] == native['driver_binary_sha256'], 'driver build binding')
    h.pin(driver_query, native['actual_driver_query_sha256'])
    query = json.loads(driver_query.read_text())
    require(query['complete'] and query['producer_source_sha256'] == native['producer_source_sha256'], 'actual query producer binding')
    h.pin(producer_ready); producer = json.loads(producer_ready.read_text())
    require(producer['complete'] and producer['producer_source_sha256'] == native['producer_source_sha256'], 'producer source identity')
    producer_source = Path(producer['source']) / 'scripts/dev/native-image-payload'
    for leaf, expected in producer['CMake_inputs'].items(): h.pin(producer_source / leaf, expected)
    if packing_receipt:
        h.pin(packing_receipt)
        receipt = json.loads(packing_receipt.read_text())
        require(receipt['complete'] and receipt['native_pack']['sha256'] == h.pin(native_path), 'packing receipt binding')
        for key in ('bulk_result', 'bulk_receipt_index', 'prepared', 'original_cache_file'):
            item = receipt[key]; h.pin(Path(item['path']), item['sha256'])
    raw, ui, upstream = (Path(cfg[k]).resolve() for k in ('raw', 'upstream_overlay', 'upstream_source'))
    contract_path = upstream / 'dm/ios/tools/overlay_contract.py'
    s = importlib.util.spec_from_file_location('pinned_upstream_overlay_contract', contract_path)
    contract = importlib.util.module_from_spec(s); s.loader.exec_module(contract)
    source_id = contract.source_fingerprint(upstream)
    require(source_id == cfg['upstream_overlay_source_sha256'], 'upstream UI source mismatch')
    ui_files = contract.verify_manifest(ui, source_id)
    h.pin(ui / 'overlay.manifest')
    for relative, expected in ui_files.items(): h.pin(ui / relative, expected)
    for relative in contract.SOURCE_INPUTS: h.pin(upstream / relative)
    for p in sorted((raw / 'data').iterdir()):
        if p.is_file(): h.pin(p)
    manifest = casc.read_manifest(raw)
    original_bytes = touch_layout._read_entry(str(raw), manifest[CACHE])
    require(digest(original_bytes) == native['complete_cache_merge']['input_sha256'], 'raw cache source differs from packed original')
    source_files = casc.read_replacements(replacements)
    require(set(source_files) == {helpers.name(r['logical'].lower()) for r in native['entries']} | {CACHE}, 'replacement selection incomplete/extra')
    require(len(source_files) == cfg['expected_selected'] + 1, 'replacement count')
    cache_bytes = source_files[CACHE].read_bytes()
    require(digest(cache_bytes) == native['complete_cache_merge']['output_sha256'], 'complete native cache hash')
    original, updated = json.loads(original_bytes), json.loads(cache_bytes)
    require(len(original) == len(updated) == cfg['expected_cache_entries'], 'cache entry count')
    selected, formats = cache_contract(original, updated, native['entries'])
    payload_bytes = 0
    for row in native['entries']:
        logical = helpers.name(row['logical'].lower()); p = source_files[logical].resolve()
        require(p.is_relative_to(pack_root.resolve()), 'replacement escapes declared native pack')
        h.pin(p, row['texture_sha256']); payload_bytes += row['image_size']
    h.pin(source_files[CACHE], native['complete_cache_merge']['output_sha256'])
    output.mkdir(parents=True)
    write(output / 'input-qualification.json', {'complete': True, 'selected': len(selected), 'replacement_files': len(source_files),
          'cache_entries': len(updated), 'selected_original_BC3_formats': formats, 'driver_sha256': native['driver_binary_sha256'],
          'producer_sha256': native['producer_source_sha256'], 'native_table_sha256': native['table_sha256'],
          'physical_payload_bytes': payload_bytes, 'upstream_UI_files': len(ui_files), 'device_executed': False})
    baseline = output / 'baseline-romfs'; helpers.link_baseline(raw, ui, baseline)
    require(touch_layout._read_entry(str(baseline), manifest[CACHE]) == original_bytes, 'UI baseline modified original cache')
    location = casc.available_location(baseline / 'data'); pack = output / 'prebuilt'
    command = [sys.executable, '-X', 'cpu_count=' + str(workers), str(Path(casc.__file__)), 'pack',
               str(baseline), str(pack), str(replacements), str(location[0]) + '-' + str(location[1])]
    with (output / 'pack.log').open('w') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    index = casc.read_prebuilt(pack / 'pack_index.tsv')
    require(set(index) == set(source_files), 'packed CASC index count/keys')
    archive_name = casc.data_filename(*location)
    (baseline / 'data' / archive_name).symlink_to((pack / archive_name).resolve())
    candidate = output / 'candidate-romfs'; casc.make_overlay(baseline, candidate, {}, index)
    proof = composition.prove_tvfs(baseline, candidate, index)
    readback = []
    for ordinal, (logical, path) in enumerate(source_files.items()):
        data = path.read_bytes()
        helpers.decoded_at(candidate, manifest[logical], index[logical], data)
        readback.append({'logical': logical, 'bytes': len(data), 'sha256': digest(data)})
        if ordinal % 1000 == 0: print('Original CASC reader exact', ordinal + 1, '/', len(source_files), flush=True)
    overlay = output / 'candidate-overlay'; shutil.copytree(ui, overlay)
    for p in (candidate / 'data').iterdir():
        if p.name == '.build.config' or not p.is_symlink() or p.name == archive_name:
            shutil.copy2(p.resolve(), overlay / 'data' / p.name)
    contract.write_manifest(overlay, source_id); actual = contract.verify_manifest(overlay, source_id)
    unchanged = {k: v for k, v in ui_files.items() if k != 'data/.build.config'}
    require(all(actual.get(k) == v for k, v in unchanged.items()), 'upstream UI/sidecar payload changed')
    runtime = output / 'runtime'; runtime.mkdir(); shutil.copy2(table, runtime / 'native-layouts.bin')
    require(h.pin(table) == digest((runtime / 'native-layouts.bin').read_bytes()), 'runtime table copy mismatch')
    write(output / 'casc-readback-proofs.json', readback)
    inputs = h.finish()
    artifacts = {str(p.relative_to(output)): {'bytes': p.stat().st_size, 'sha256': hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()}
                 for p in sorted(output.rglob('*')) if p.is_file() and not p.is_symlink()}
    result = {'schema': 1, 'complete': True, 'offline_only': True, 'device_access': False, 'deployed': False,
              'game_binary_modified': False, 'texture_encoder_used': False, 'container_implementation': 'original casc_overlay pack/make_overlay + astc_compose.prove_tvfs + original touch_layout CASC reader',
              'driver_sha256': native['driver_binary_sha256'], 'producer_source_sha256': native['producer_source_sha256'],
              'native_pack_content_sha256': native['pack_content_sha256'], 'native_table_sha256': native['table_sha256'],
              'selected': len(selected), 'all_replacements_read_back_exact': len(readback), 'full_cache_entries': len(updated),
              'fallback_entries_unchanged': len(updated) - len(selected), 'selected_original_BC3_formats': formats,
              'native_physical_payload_bytes': payload_bytes, 'upstream_UI_source_sha256': source_id,
              'upstream_UI_payload_files_unchanged_except_config': len(unchanged), 'TVFS_CFT_root_config_proof': proof,
              'candidate_overlay': str(overlay), 'candidate_overlay_bytes': sum((overlay / p).stat().st_size for p in actual),
              'candidate_manifest_sha256': digest((overlay / 'overlay.manifest').read_bytes()),
              'runtime_native_table': str(runtime / 'native-layouts.bin'), 'commands': [command],
              'source_fingerprints': inputs, 'artifacts': artifacts,
              'runtime_acceptance': 'NOT_TESTED; native pack identity/capability/resident/lazy and real-game acceptance remain separate gates; never overwrite accepted resources implicitly'}
    write(output / 'composition.json', result)
    write(output / 'composition.sha256.json', {'composition.json': digest((output / 'composition.json').read_bytes())})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--native-pack', type=Path, required=True)
    p.add_argument('--driver-build', type=Path, required=True)
    p.add_argument('--driver-query', type=Path, required=True)
    p.add_argument('--producer-ready', type=Path, required=True)
    p.add_argument('--packing-receipt', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--workers', type=int, choices=range(1, 9), default=4)
    a = p.parse_args()
    result = build(a.config.resolve(), a.native_pack.resolve(), a.output.resolve(), a.driver_build.resolve(),
                   a.driver_query.resolve(), a.producer_ready.resolve(), a.workers,
                   a.packing_receipt.resolve() if a.packing_receipt else None)
    print(json.dumps({k: v for k, v in result.items() if k not in ('artifacts', 'source_fingerprints')}, indent=2))


if __name__ == '__main__':
    main()
