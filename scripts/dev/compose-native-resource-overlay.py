#!/usr/bin/env python3
"""Compose ONE verified native family over fullASTC4; no deployment or game patch.

Reuse original CASC CLI and existing TVFS/roundtrip proofs. Native payload/table
come from the exact driver producer and original-NVN ordinary-UID acceptance.
The result remains blocked for game deployment until runtime streaming gates
and actual Builder/storage ordering are accepted independently.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'dm/tools'))
import astc_compose as comp
import casc_overlay as casc
import touch_layout

CACHE = 'data/hd/texture_desc_cache.json'
DRIVER = '6849875ce017b19a8913ed0fb3b9a1edcbfb13b44a25ad32d3bfbec7d1602338'
PRODUCER = '043d009065b301a7cb05bab2c0d9517feb39f9464fca013929f100fe34802fe2'


def compose(a):
    require = comp.require
    hashes = comp.Hashes()
    raw, pack, out = a.raw.resolve(), a.native_pack.resolve(), a.out.resolve()
    require(not out.exists(), 'output must be new; no partial-cache reuse')
    require(not subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True),
            'compose source must be frozen and clean')
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    record = json.loads((pack / 'native-pack.json').read_text())
    require(record['complete'] and record['family_protocol'] == 2 and len(record['entries']) == 1,
            'exactly one complete family-v2 native entry required')
    require(record['driver_binary_sha256'] == DRIVER and record['producer_source_sha256'] == PRODUCER,
            'unverified native driver/producer')
    acceptance = json.loads(a.runtime_acceptance.read_text())
    require(acceptance['status'] == 'independently verified real ordinary-UID original-NVN native-family GPU bytes PASS' and
            acceptance['driver_SHA256'] == DRIVER and len(acceptance['phases']) == 2,
            'real original-NVN runtime acceptance required (not API-only producer)')
    entry = record['entries'][0]
    logical = entry['logical']
    require(casc.norm_path(logical) == logical and logical.endswith('.texture'), 'invalid selected logical path')
    require(entry['id'] == 1 and entry['layout8'] == '444d4e0201000000' and entry['thumbnail_mip'] > 0,
            'one exact full+tail family required')
    require(entry['usage'] == 0x400007 and entry['alignment'] == 4096,
            'this acceptance only covers the declared transfer recipe/alignment')
    table, texture, cache = pack / 'native-layouts.bin', pack / logical, pack / CACHE
    require(hashes.file(table) == record['table_sha256'] and table.stat().st_size == 1216, 'native table changed')
    require(hashes.file(texture) == entry['texture_sha256'] and
            hashes.file(cache) == record['complete_cache_merge']['output_sha256'], 'native outputs changed')
    require(hashes.file(a.baseline_cache) == record['complete_cache_merge']['input_sha256'], 'wrong baseline cache')
    require(hashes.file(a.baseline_texture) == entry['source_texture_sha256'], 'wrong original full4 texture')
    before, after = json.loads(a.baseline_cache.read_text()), json.loads(cache.read_text())
    require(len(before) == len(after) == 31072, 'full descriptor cache required')
    changed = []
    for old, new in zip(before, after):
        require(old['filename'] == new['filename'], 'cache reordered')
        if old == new:
            continue
        require(old['filename'].lower() == logical and
                {k: v for k, v in old.items() if k != 'layoutData'} ==
                {k: v for k, v in new.items() if k != 'layoutData'}, 'non-layout descriptor/thumbnail changed')
        require(int(new['layoutData']).to_bytes(8, 'little').hex() == entry['layout8'], 'cache marker mismatch')
        changed.append(logical)
    require(changed == [logical], 'exact one selected descriptor change required')
    declared = casc.read_replacements(pack / 'replacements.tsv')
    require(set(declared) == {logical, CACHE} and declared[logical].resolve() == texture and
            declared[CACHE].resolve() == cache, 'replacement declaration mismatch')
    require(hashes.file(pack / 'replacements.tsv') == record['replacements_sha256'], 'replacements changed')
    baseline = a.baseline_overlay.resolve()
    baseline_record = json.loads(a.baseline_overlay_record.read_text())
    comp.verify_map(baseline, baseline_record['expected_hashes'], hashes)
    manifest = json.loads(a.baseline_pack_manifest.read_text())
    require(manifest['name'] == '00-00000008.data' and
            manifest['sha256'] == hashes.file(baseline / 'data' / manifest['name']) and
            manifest['size'] == (baseline / 'data' / manifest['name']).stat().st_size,
            'baseline full4 archive differs')
    require(hashes.file(baseline / 'pack.json') == hashes.file(a.baseline_pack_manifest), 'baseline pack metadata differs')
    header, original_rows = comp.index_rows(a.baseline_index)
    original = casc.read_prebuilt(a.baseline_index)
    require({logical, CACHE} <= set(original), 'selected inputs absent from full4 archive')
    source_manifest = casc.read_manifest(raw)
    for name, expected in [(logical, entry['source_texture_sha256']), (CACHE, hashes.file(a.baseline_cache))]:
        key, encoded, decoded = original[name]
        require(casc.data_filename(key[9], key[10]) == manifest['name'] and
                int.from_bytes(key[11:], 'big') + encoded <= manifest['size'], 'baseline key outside archive8')
        reference = a.baseline_texture if name == logical else a.baseline_cache
        row = list(source_manifest[name]);row[2:8] = [str(key[9]), str(key[10]), hashlib.md5(reference.read_bytes()).hexdigest(), str(decoded), key.hex(), str(encoded)]
        require(comp.byte_sha(touch_layout._read_entry(str(baseline), row)) == expected,
                'baseline CASC content differs from declared native source: ' + name)
    for name in (comp.PACK9, comp.ROOT10):
        require(not (raw / 'data' / name).exists() and not (baseline / 'data' / name).exists(),
                'archive location already occupied: ' + name)
    require(sys.version_info >= (3, 13), 'Python3.13+ required for bounded original CASC workers')
    out.mkdir(parents=True)
    commands = [comp.run_casc(['pack', raw, out / 'pack9', pack / 'replacements.tsv', '0-9'], out, 'pack.log')]
    updates = casc.read_prebuilt(out / 'pack9/pack_index.tsv')
    _, update_rows = comp.index_rows(out / 'pack9/pack_index.tsv')
    require(set(updates) == {logical, CACHE}, 'new archive changed unexpected entries')
    combined = original_rows | update_rows
    require(all(combined[n] == row for n, row in original_rows.items() if n not in updates), 'unselected index changed')
    (out / 'merged-pack-index.tsv').write_text(header + '\n' + '\n'.join(combined.values()) + '\n')
    source = out / 'composed-source';(source / 'data').mkdir(parents=True)
    for p in raw.iterdir():
        if p.name != 'data':(source / p.name).symlink_to(p.resolve(), target_is_directory=p.is_dir())
    for p in (raw / 'data').iterdir():
        if p.name not in ('.build.config', 'build_manifest.psv'):
            (source / 'data' / p.name).symlink_to(p.resolve(), target_is_directory=p.is_dir())
    for p in (baseline / 'data').iterdir():
        target = source / 'data' / p.name;target.unlink(missing_ok=True);target.symlink_to(p.resolve())
    (source / 'data' / comp.PACK9).symlink_to((out / 'pack9' / comp.PACK9).resolve())
    rows, replaced = [], []
    for line in (raw / 'data/build_manifest.psv').read_text().splitlines():
        fields = line.split('|');name = casc.norm_path(fields[9]) if len(fields) >= 11 else ''
        if name in updates:fields[6] = original[name][0].hex();replaced.append(name)
        rows.append('|'.join(fields))
    require(set(replaced) == set(updates) and len(replaced) == len(updates), 'virtual manifest lookup mismatch')
    (source / 'data/build_manifest.psv').write_text('\n'.join(rows) + '\n')
    for name, (key, encoded, decoded) in updates.items():
        row = list(source_manifest[name]);row[2:8] = [str(key[9]), str(key[10]), hashlib.md5(declared[name].read_bytes()).hexdigest(), str(decoded), key.hex(), str(encoded)]
        require(comp.byte_sha(touch_layout._read_entry(str(source), row)) == hashes.file(declared[name]),
                'native archive content roundtrip failed: ' + name)
    require(casc.available_location(source / 'data') == (0, 10), 'unexpected root TVFS archive')
    empty = out / 'empty.tsv';empty.write_text('')
    commands.append(comp.run_casc([source, out / 'casc-overlay', empty, '--prebuilt', out / 'pack9/pack_index.tsv'], out, 'overlay.log'))
    proof = comp.prove_tvfs(source, out / 'casc-overlay', updates)
    deploy = out / 'romfs_overlay_native';deploy.mkdir();comp.link_files(baseline, deploy)
    for name, src in {'.build.config': out / 'casc-overlay/data/.build.config',
                      comp.ROOT10: out / 'casc-overlay/data' / comp.ROOT10,
                      comp.PACK9: out / 'pack9' / comp.PACK9}.items():
        target = deploy / 'data' / name;target.unlink(missing_ok=True);os.link(src, target)
    for name, digest in baseline_record['expected_hashes'].items():
        if name != 'data/.build.config':require(hashes.file(deploy / name) == digest, 'inherited UI/full4 changed')
    comp.verify_map(baseline, baseline_record['expected_hashes'], hashes)
    shutil.copy2(table, out / 'native-layouts.bin')
    sources = [Path(__file__), Path(comp.__file__), Path(casc.__file__), Path(touch_layout.__file__),
               pack / 'native-pack.json', pack / 'replacements.tsv', table, texture, cache,
               a.baseline_cache, a.baseline_texture, a.baseline_index, a.baseline_pack_manifest, a.baseline_overlay_record,
               a.runtime_acceptance, raw / 'data/.build.config', raw / 'data/build_manifest.psv']
    result = {'complete': True, 'not_deployed': True, 'source_revision': revision, 'source_clean': True,
              'CASC_workers': 2, 'commands': commands, 'selected': [logical], 'native_family_count': 1,
              'table_sha256': hashes.file(out / 'native-layouts.bin'),
              'pack_content_sha256': record['pack_content_sha256'], 'producer_source_sha256': PRODUCER,
              'driver_binary_sha256': DRIVER, 'runtime_source': acceptance['source_commit'],
              'original_NVN_owned_actual_pass': True, 'game_acceptance': False, 'deployment_allowed': False,
              'actual_game_Builder_and_streaming_pending': True, 'default_enabled': False,
              'only_selected_cache_layoutData_changed': True, 'all_original_thumbnails_exact': True,
              'unselected_index_rows_exact': True, 'UI_and_full4_archive_exact': True, 'TVFS_proof': proof,
              'physical_image_bytes': entry['image_size'], 'alignment': entry['alignment'],
              'table_bytes': table.stat().st_size, 'payload_resident_or_performance_savings_claim': False,
              'inputs': {str(p.resolve()): hashes.file(p) for p in sources},
              'overlay': str(deploy), 'overlay_hashes': comp.file_map(deploy, hashes),
              'new_archives': {name: hashes.file(deploy / 'data' / name) for name in (comp.PACK9, comp.ROOT10)},
              'merged_index_sha256': hashes.file(out / 'merged-pack-index.tsv'),
              'native_startup_options': {'shared_guest_heap': True, 'vulkan_provider': 'panvk',
                                         'native_resource_table': '<host-private-native-layouts.bin>',
                                         'native_resource_pack_sha256': record['pack_content_sha256'],
                                         'native_resource_producer_sha256': PRODUCER},
              'remaining': ['Fresh-format and original packaged FS-lease protection fixes composed/canonical verified',
                            'Actual game resource Builder/4KiB allocation and full/tail streaming acceptance',
                            'No gamebinary/NSO patched or generated here; no devices used']}
    (out / 'composition.json').write_text(json.dumps(result, indent=2) + '\n')
    print(out / 'composition.json')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('raw', 'native-pack', 'out', 'baseline-cache', 'baseline-texture', 'baseline-index',
                 'baseline-pack-manifest', 'baseline-overlay', 'baseline-overlay-record', 'runtime-acceptance'):
        p.add_argument('--' + name, type=Path, required=True)
    compose(p.parse_args())


if __name__ == '__main__':
    main()
