#!/usr/bin/env python3
"""Offline, authenticated 32-entry ASTC6 substitution in the original 6217 family.

Reuse the original strict packer and original bulk composer's CASC/TVFS helpers.
All 6217 containers/table entries are rebuilt from original source declarations
and actual producer outputs. No bytes of an existing table/archive are patched.
The frozen qualification policy is re-evaluated over each original mip report;
its large encoder report remains hash-authenticated by the reviewed preparation.
No device operation or new game/GPU acceptance is provided by this tool.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
# Local immutable evidence is shared by isolated source worktrees.
DATA = Path('/home/dl/d2r-mobile')
OLD = DATA / '.local/build/turnip-native-bulk-6217-f830-v1'
OLD_PREP = DATA / '.local/build/turnip-native-bulk-act1-character-full4-v1-inputs/batch-prepare.json'
NEW_PREP = DATA / '.local/build/native-qualified-astc6-pilot32-v1/inputs/batch-prepare.json'
ACTUAL = DATA / '.local/logs/native-qualified-astc6-pilot32-f830-q-v1'
PRODUCER = '8064b80022370de2832964ee22015539441981e7272270b0ff01f5b6c6062ca3'
PINS = {
    OLD / 'native-overlay/composition.json': '9c13e4a8eb2e72877e81f08026b29a6bb77933b5037484a1862a5261ff4b961a',
    OLD / 'native-pack/native-pack.json': '8f9562ee202431da15e112577d50762d17db384577cfafdefe8f9930ab1f423e',
    OLD_PREP: 'e68a8d45226c2820d2b1fc4d3a3f67b62a6c88e0950b86cc7c837a2da07143c8',
    NEW_PREP: '157ba1ced6f267c998a9a2b934dac5bd85a7afe04773dc85c2c8ac83c1e05b4b',
    DATA / '.local/worktrees/native-astc-linear-compose-v1/scripts/dev/compose-bulk-turnip-native-overlay.py': '7959b20b6e6a9766918c5ea22c58861f6392e86215506fac72b94c5255a8b646',
    DATA / '.local/logs/native-qualified-astc6-ca28208-root-source-review.json': 'f48424dcc4eeaa406515a35ac634be5a7da1b0f9e020389badfd4c1f98936f18',
    DATA / '.local/logs/native-qualified-astc6-ca28208-source-ready-agent.json': 'ba8972cf9d740ca77bd20ba6c01d218217fe392d5e7f57efff5e41b5d9599fd5',
}
PACKER_SHA = 'f4ae133c09071606a48fce987ac9c1da442266e89d217a386c471eea8ee1b5cb'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def normalized(name):
    require(isinstance(name, str) and name.startswith('data/') and
            not Path(name).is_absolute() and '..' not in Path(name).parts and
            not any(c in name for c in '\\\t\r\n'), 'unsafe logical filename')
    return name.lower()


def ordered_substitution(old_entries, new_entries, old_specs, new_coverage):
    """Identity comparison is case-insensitive; serialized names stay intact."""
    old_names = [normalized(e['logical']) for e in old_entries]
    new_names = [normalized(e['logical']) for e in new_entries]
    require(len(set(old_names)) == len(old_names) and len(set(new_names)) == len(new_names),
            'duplicate normalized logical identity')
    require(set(new_names) <= set(old_names), 'replacement outside original family')
    require(len(old_specs) == len(old_entries), 'incomplete old specification order')
    replacements = dict(zip(new_names, new_entries))
    result = []
    for name, original in zip(old_names, old_specs):
        if name in replacements:
            entry = replacements[name]
            require(entry['id'] == hashlib.sha256(name.encode()).hexdigest(), 'replacement ID differs')
            result.append({'source': entry['source']['path'], 'texture': entry['texture']['path'],
                           'baked': new_coverage[entry['id']]['baked']})
        else:
            result.append(dict(original))
    return old_names, replacements, result


def preserved_entry(old_detail, new_detail, old_table, new_table, ordinal, count):
    require(old_detail == new_detail, 'unselected pack detail changed')
    offset = 160 + ordinal * 1040
    require(old_table[offset:offset + 1040] == new_table[offset:offset + 1040],
            'unselected native recipe/table bytes changed')
    offset = 160 + count * 1040 + ordinal * 16
    require(old_table[offset:offset + 16] == new_table[offset:offset + 16],
            'unselected original-tail family changed')


def actual_gate(composer, root, prepared, prepared_path, ready, acceptance, hashes):
    result = read(root / 'result.json')
    hashes.file(root / 'result.json')
    require(result['stage'] == 'actual-turnip-native-resource-batches-owned-passed' and
            not result['cleanup_errors'] and result['original_before'] == result['original_after'] and
            all(result[k] for k in ['original_unchanged', 'fixture_PID_absent', 'fixture_uninstalled',
                                   'fixture_tmp_absent', 'owned_clients_stopped']), 'unclean actual transaction')
    require(result['prepare_manifest_sha256'] == hashes.file(prepared_path) and
            result['entry_count'] == len(prepared['entries']) and
            result['batch_count'] == len(prepared['batches']) and result['first_batch'] == 0 and
            result['producer_source_sha256'] == PRODUCER and
            result['driver_sha256'] == acceptance['driver_sha256'], 'actual complete selection differs')
    index_path = root / 'actual-batch-receipt-index.json'
    index = read(index_path)
    require(index['entries'] == len(prepared['entries']), 'actual index entry count differs')
    query = Path(index['results'][0]['path']).parent / 'items' / prepared['batches'][0]['entry_ids'][0] / 'native-resource.json'
    coverage = composer.producer_gate.actual_batches_gate(index_path, result['receipt_index_sha256'], ready,
        PRODUCER, prepared, hashes.file(prepared_path), acceptance, query, hashes)
    return coverage, query


def qualified_gate(prepared, old_prepared, hashes):
    selection = prepared['selection']
    require(selection['kind'] == 'explicit-qualified-ASTC6-native-replacement-v1' and
            len(prepared['entries']) == selection['expected_count'] == 32 and
            selection['formats'] == [99, 85], 'expected reviewed ASTC6 pilot32')
    # This prepared manifest and its policy/large report were already root-reviewed.
    # Authenticate every declared source before repeating the per-item quality check.
    for key in ['logical_manifest', 'qualified_composition', 'encoder_job', 'encoder_report',
                'quality_evaluator', 'original_native_source_pack', 'policy_tool', 'array_closure']:
        hashes.record(selection[key])
    require(selection['original_native_source_pack']['sha256'] == hashes.file(OLD_PREP), 'old family ancestry differs')
    context = prepared['source_context']
    require(context['original_build_manifest'] == old_prepared['source_context']['original_build_manifest'], 'original game build differs')
    for item in context['tool_inputs']:
        hashes.record(item)
    for key in ['source_index', 'NVN_layout_library', 'full_astc_provenance']:
        hashes.record(context[key])
    for value in context['verified_archives'].values():
        for item in value.values():
            hashes.record(item)
    policy_path = Path(selection['quality_evaluator']['path'])
    sys.path.insert(0, str(policy_path.parent))
    evaluator = load(policy_path, 'original_qualified_astc6_evaluator')
    expected_policy = {'min_rgb_psnr': 32.0, 'max_alpha_error': 8,
                       'max_alpha_coverage_fraction': 0.001, 'constant_alpha_max_error': 0}
    require(selection['original_quality_policy'] == expected_policy, 'original quality policy differs')
    proofs = {normalized(p['logical']): p for p in selection['quality_item_proofs']}
    require(len(proofs) == 32, 'quality proof coverage differs')
    for entry in prepared['entries']:
        name = normalized(entry['logical'])
        require(name in proofs, 'missing selected quality proof')
        proof = proofs[name]
        hashes.record(proof['original_source'])
        for item in proof['encoded_artifacts'].values():
            hashes.record(item)
        for key in ['source', 'texture', 'input', 'prepare_report']:
            hashes.record(entry[key])
        source = read(entry['source']['path'])
        approved = proof['encoded_artifacts']
        require(source['source_descriptor'] == read(approved['descriptor.json']['path']) and
                hashes.file(entry['texture']['path']) == approved['texture']['sha256'], 'approved encoded resource differs')
        require(not evaluator.quality_failures(read(approved['quality.json']['path']), expected_policy), 'original per-mip quality rejected')
        original = read(proof['original_source']['path'])['source_descriptor']
        descriptor = source['source_descriptor']
        require(original['filename'] == descriptor['filename'] and
                original['format'] == {99: 95, 85: 81}[descriptor['format']] and
                {k: v for k, v in original.items() if k not in ['format', 'thumbnailBase64']} ==
                {k: v for k, v in descriptor.items() if k not in ['format', 'thumbnailBase64']},
                'replacement changes original serialized name/topology')
        require(source['game_format'] in [99, 85] and source['layout_roundtrip_useful_blocks_exact'] is True and
                source['decoder_or_encoder_used'] is False, 'unexpected source preparation')


def compose(output):
    require(not output.exists(), 'new output required; partial outputs cannot be reused')
    require(not subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True), 'source must be committed and clean')
    composer_path = next(p for p in PINS if p.name == 'compose-bulk-turnip-native-overlay.py')
    require(sha(composer_path) == PINS[composer_path], 'original composer changed')
    composer = load(composer_path, 'original_native_bulk_composer')
    h = composer.Fingerprints()
    for path, expected in PINS.items():
        require(h.file(path) == expected, 'frozen evidence changed: ' + str(path))
    old_comp = read(OLD / 'native-overlay/composition.json')
    old_pack = read(OLD / 'native-pack/native-pack.json')
    require(old_comp['complete'] and old_comp['entry_count'] == 6217 and
            old_pack['complete'] and len(old_pack['entries']) == 6217, 'original complete family required')
    for path, expected in old_comp['source_fingerprints'].items():
        require(h.file(path) == expected, 'original composition source changed: ' + path)
    for relative, expected in old_comp['exact_generated_overlay_files'].items():
        require(h.file(OLD / 'native-overlay/romfs_overlay' / relative) == expected, 'original overlay changed')
    command_path = OLD / 'composer-command.json'
    h.file(command_path)
    previous_command = read(command_path)
    arg = lambda key: Path(previous_command[previous_command.index('--' + key) + 1]).resolve()
    acceptance = composer.receipt_gate(arg('acceptance'), old_comp['one_material_functional_receipt_sha256'], h)
    ready = composer.producer_gate.source_ready_gate(arg('producer-source-ready'),
        old_comp['producer_identity_lineage']['current_source_ready_sha256'], PRODUCER, acceptance, h)
    old_prepared = read(OLD_PREP)
    new_prepared = read(NEW_PREP)
    qualified_gate(new_prepared, old_prepared, h)
    old_coverage, _ = actual_gate(composer, DATA / '.local/logs/native-astc-linear-bulk-q-all6217-v1', old_prepared, OLD_PREP, ready, acceptance, h)
    new_coverage, query = actual_gate(composer, ACTUAL, new_prepared, NEW_PREP, ready, acceptance, h)
    old_specs_path = OLD / 'entries.json'
    require(h.file(old_specs_path) == '0ebee0cef3dcc3543651baa957d5eaa582b28bfb11d2ef68dde00f5b1b589b69', 'old ordered specifications changed')
    old_specs = read(old_specs_path)
    names, replacements, specs = ordered_substitution(old_prepared['entries'], new_prepared['entries'], old_specs, new_coverage)
    require(names == [normalized(d['logical']) for d in old_pack['entries']], 'old prepared/family order differs')
    for entry, spec in zip(old_prepared['entries'], old_specs):
        require(Path(spec['baked']).resolve() == Path(old_coverage[entry['id']]['baked']) and
                all(Path(spec[k]).resolve() == Path(entry[k]['path']).resolve() for k in ['source', 'texture']), 'old specs outside original actual coverage')
    packer_path = ROOT / 'scripts/dev/pack-native-texture.py'
    require(h.file(packer_path) == PACKER_SHA == old_pack['tool_sha256'], 'original strict packer changed')
    packer = load(packer_path, 'original_strict_native_packer')
    output.mkdir(parents=True)
    entries_path = output / 'entries.json'
    write(entries_path, specs)
    pack_command = [sys.executable, str(packer_path), '--entries', str(entries_path),
                    '--driver-manifest', str(arg('driver-manifest')), '--driver-query', str(query), '--output', str(output / 'native-pack')]
    write(output / 'packer-command.json', pack_command)
    with (output / 'packer.stdout.txt').open('w') as log:
        subprocess.run(pack_command, check=True, stdout=log, stderr=subprocess.STDOUT)
    record = read(output / 'native-pack/native-pack.json')
    table = (output / 'native-pack/native-layouts.bin').read_bytes()
    old_table = (OLD / 'native-pack/native-layouts.bin').read_bytes()
    require(h.file(OLD / 'native-pack/native-layouts.bin') == old_pack['table_sha256'], 'original table changed')
    composer.authenticate_table(old_table, old_pack)
    composer.authenticate_table(table, record)
    require(len(table) == len(old_table) == 6565312 and record['identity'] == old_pack['identity'] and
            record['producer_source_sha256'] == old_pack['producer_source_sha256'] == PRODUCER and
            record['driver_binary_sha256'] == old_pack['driver_binary_sha256'] == acceptance['driver_sha256'], 'family identity/size differs')
    raw, ui = arg('raw'), arg('ui-overlay')
    ui_proof, manifest, original_cache, cache_map, baseline, positions = composer.original_baseline(raw, ui, arg('ui-proof'), names, h)
    old_cache_path = OLD / 'native-overlay/files/cache'
    h.file(old_cache_path)
    old_cache = read(old_cache_path)
    patched = list(old_cache)
    require(len(old_cache) == len(original_cache) == 31072 and
            [d['filename'] for d in old_cache] == [d['filename'] for d in original_cache], 'old native cache identity/order differs')
    out = output / 'native-overlay'
    out.mkdir()
    inputs = out / 'inputs'
    composer.one.link_inputs(raw, ui, inputs)
    require(bytes(composer.casc.load_vfs1(inputs)) == baseline, 'original BC/UI baseline differs')
    location = composer.casc.available_location(inputs / 'data')
    archive_name = composer.casc.data_filename(*location)
    prebuilt = out / 'prebuilt'
    prebuilt.mkdir()
    archive = prebuilt / archive_name
    (inputs / 'data' / archive_name).symlink_to(archive)
    updated, entry_proofs = {}, []
    with archive.open('wb') as stream, (prebuilt / 'pack_index.tsv').open('w') as index:
        index.write('# pack\t' + archive_name + '\n')
        for ordinal, (name, spec, detail, old_detail) in enumerate(zip(names, specs, record['entries'], old_pack['entries'])):
            wire, texture, descriptor, reproduced, identity = packer.load_entry(spec, ordinal + 1)
            require(reproduced == detail and identity.hex() == record['identity'], 'strict packer reproduction differs')
            require(wire == table[160 + ordinal * 1040:160 + (ordinal + 1) * 1040], 'reproduced table record differs')
            require(descriptor['filename'] == old_cache[cache_map[name]]['filename'] == detail['logical'], 'serialized original filename changed')
            if name not in replacements:
                preserved_entry(old_detail, detail, old_table, table, ordinal, len(names))
                require(descriptor == old_cache[cache_map[name]], 'unselected descriptor/thumbnail changed')
            else:
                require(detail['id'] == old_detail['id'] == ordinal + 1 and detail['image_size'] < old_detail['image_size'], 'replacement family ID/byte reduction differs')
                require(descriptor == {**read(spec['source'])['source_descriptor'], 'layoutData': old_cache[cache_map[name]]['layoutData']}, 'selected descriptor beyond approved ASTC6 and original family marker')
                patched[cache_map[name]] = descriptor
                entry_proofs.append({'id': ordinal + 1, 'logical': name, 'old_native_bytes': old_detail['image_size'],
                                     'native_bytes': detail['image_size'], 'source': replacements[name]['source'],
                                     'texture': replacements[name]['texture'], 'actual_baked': spec['baked']})
            packaged = output / 'native-pack' / detail['logical']
            require(h.file(packaged) == detail['texture_sha256'], 'packaged output changed')
            require(h.file(OLD / 'native-pack' / old_detail['logical']) == old_detail['texture_sha256'], 'old packaged output changed')
            result = composer.encode_one(stream, texture, manifest[name][8], location)
            updated[name] = result
            key, encoded, decoded = result
            index.write(f'{name}\t{key.hex()}\t{encoded}\t{decoded}\n')
            composer.read_indexed(inputs, manifest[name], {'ekey': key.hex(), 'encoded_bytes': encoded, 'encoding': manifest[name][8]}, texture)
    require(len(entry_proofs) == 32 and all(a == b for a, b in zip(old_cache, patched) if normalized(a['filename']) not in replacements), 'unselected cache changed')
    files = out / 'files'
    files.mkdir()
    cache_path = files / 'cache'
    cache_path.write_bytes(composer.canonical(patched))
    (files / 'native-layouts.bin').write_bytes(table)
    overlay, proof = composer.finalize_overlay(out, inputs, archive_name, archive, cache_path, updated, positions, manifest, ui_proof, h)
    # Bind the old native complete cache back to the authenticated old CASC CFT.
    old_vfs = bytes(composer.casc.load_vfs1(OLD / 'native-overlay/romfs_overlay'))
    cp = positions[composer.CACHE]
    composer.one.decoded(OLD / 'native-overlay/romfs_overlay', manifest[composer.CACHE], old_vfs[cp:cp + 25], old_cache_path.read_bytes())
    generated = {str(p.relative_to(output)): sha(p) for p in output.rglob('*') if p.is_file() and not p.is_symlink()}
    h.file(Path(__file__))
    result = {'schema': 1, 'complete': True, 'source_revision': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'source_fingerprints': h.finish(), 'entry_count': 6217, 'replaced_entries': 32, 'unchanged_native_entries': 6185,
        'family_ID_order_and_serialized_names_preserved': True, 'unselected_table_recipe_family_texture_descriptor_thumbnail_exact': True,
        'cache_entries': 31072, 'unchanged_cache_entries': 31040, 'selected_quality_approved_ASTC6_thumbnails_exact': True,
        'old_native_payload_bytes': old_comp['native_payload_bytes'], 'native_payload_bytes': sum(d['image_size'] for d in record['entries']),
        'old_selected_native_bytes': sum(p['old_native_bytes'] for p in entry_proofs), 'selected_native_bytes': sum(p['native_bytes'] for p in entry_proofs),
        'native_table_sha256': sha(output / 'native-pack/native-layouts.bin'), 'native_table_bytes': len(table), 'native_pack_content_sha256': record['pack_content_sha256'],
        'driver_sha256': record['driver_binary_sha256'], 'driver_identity': record['identity'], 'producer_source_sha256': PRODUCER,
        'selected_logicals_sha256': composer.digest(composer.canonical(names)), 'entry_proofs': entry_proofs, 'original_TVFS_proof': proof,
        'original_CASC_roundtrip': True, 'upstream_UI_preserved': True, 'exact_generated_files': generated,
        'original_packer_unchanged': True, 'original_CASC_encoder_and_prebuilt_TVFS': True, 'source_or_payload_byte_patch': False,
        'game_binary_modified': False, 'device_access': False, 'game_modules_executed': False, 'game_acceptance': False, 'deploy_allowed': False,
        'remaining': ['actual ASTC6 new-source original NVN consumer qualification', 'actual game Builder/storage/streaming/visual qualification',
                      'official tool profiles for PSS/DRAM/heat/6GB working set; payload counts are not these measurements']}
    write(out / 'composition.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = compose(args.output.resolve())
    print(json.dumps({k: result[k] for k in ['complete', 'entry_count', 'replaced_entries', 'native_payload_bytes', 'native_table_sha256', 'deploy_allowed']}))


if __name__ == '__main__':
    main()
