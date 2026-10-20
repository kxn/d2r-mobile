#!/usr/bin/env python3
"""Compose the identical selected standard NVN ASTC4 set, without native markers.

Offline control for a bulk native-image experiment. Uses the same prepared
source manifest and original BC/upstream UI baseline as the native candidate;
unselected textures stay BC. This is not the whole 27k ASTC resource pack.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bulk_native_common', Path(__file__).with_name('compose-bulk-turnip-native-overlay.py'))
bulk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bulk)
require = bulk.require


def control_descriptor(before, astc):
    changed = {key for key in before.keys() | astc.keys() if before.get(key) != astc.get(key)}
    require(changed <= bulk.ALLOWED_FIELDS and
            (before['format'], astc['format']) in {(61, 95), (62, 81)}, 'unsupported standard ASTC control descriptor')
    marker = astc['layoutData'].to_bytes(8, 'little')
    require(marker[0] <= 5 and marker[:3] != b'DMN', 'control contains native packaged layout marker')
    require(before.get('thumbnailMip') == astc.get('thumbnailMip') and astc.get('thumbnailMip', 0) > 0,
            'control original tail differs')
    return sorted(changed)


def compose(a):
    out, raw, ui = [p.resolve() for p in [a.out, a.raw, a.ui_overlay]]
    require(not out.exists(), 'output must be new; partial composition is not resumed')
    require(not subprocess.check_output(['git', '-C', str(ROOT), 'diff', 'HEAD', '--name-only'], text=True).strip(),
            'tracked tool source must be frozen')
    h = bulk.Fingerprints()
    prepared, quality, source_cache_path = bulk.source_gate(a.source_pack_manifest,
        a.source_pack_manifest_sha256, h, a.artifact_root.resolve())
    context = prepared['source_context']
    require(Path(context['original_build_manifest']['path']).resolve() == raw / 'data/build_manifest.psv',
            'original BC build differs')
    names = bulk.unique_entries(prepared['entries'])
    ui_proof, manifest, old, old_map, baseline, positions = bulk.original_baseline(raw, ui, a.ui_proof, names, h)
    require(h.file(a.entries) == a.entries_sha256, 'ordered source specifications changed')
    specs = json.loads(a.entries.read_text())
    require(isinstance(specs, list) and len(specs) == len(names), 'incomplete ordered source entries')
    out.mkdir()
    inputs = out / 'inputs'
    bulk.one.link_inputs(raw, ui, inputs)
    require(bytes(bulk.casc.load_vfs1(inputs)) == baseline, 'layered upstream UI input differs')
    astc_inputs = out / 'astc-inputs'
    (astc_inputs / 'data').mkdir(parents=True)
    for name, item in context['verified_archives'].items():
        require(name == Path(name).name, 'unsafe source archive name')
        (astc_inputs / 'data' / name).symlink_to(Path(item['archive']['path']).resolve())
    source_index = bulk.casc.read_prebuilt(Path(context['source_index']['path']))
    bulk.source_index_matches(source_index, bulk.CACHE, context['source_cache'])
    cache = source_cache_path.read_bytes()
    bulk.read_indexed(astc_inputs, manifest[bulk.CACHE], context['source_cache'], cache)
    source_cache = json.loads(cache)
    astc_descriptors = {bulk.normalized(d['filename'].lower()): d for d in source_cache}
    require(len(astc_descriptors) == len(source_cache), 'duplicate complete ASTC cache entry')
    location = bulk.casc.available_location(inputs / 'data')
    archive_name = bulk.casc.data_filename(*location)
    prebuilt = out / 'prebuilt'
    prebuilt.mkdir()
    archive = prebuilt / archive_name
    (inputs / 'data' / archive_name).symlink_to(archive)
    patched = list(old)
    updated, proofs = {}, []
    bytes_total = 0
    for_import = ['original_build_manifest', 'source_index', 'verified_archives', 'source_cache', 'tool_inputs', 'NVN_layout_library']
    with archive.open('wb') as stream, (prebuilt / 'pack_index.tsv').open('w') as index:
        index.write('# pack\t' + archive_name + '\n')
        for name, specification, entry in zip(names, specs, prepared['entries']):
            require(entry['id'] == bulk.digest(name.encode()), 'prepared logical ID differs')
            for key in ['source', 'texture', 'input', 'prepare_report']:
                h.record(entry[key])
            require(Path(specification['source']).resolve() == Path(entry['source']['path']).resolve() and
                    Path(specification['texture']).resolve() == Path(entry['texture']['path']).resolve(),
                    'ordered source specification differs')
            source = json.loads(Path(specification['source']).read_text())
            require(all(source[key] == context[key] for key in for_import), 'per-entry source provenance differs')
            require(Path(specification['source']).with_name('resource-input.bin').resolve() == Path(entry['input']['path']).resolve(),
                    'linear input differs')
            descriptor = source['source_descriptor']
            require(descriptor == astc_descriptors[name] and source['layout_roundtrip_useful_blocks_exact'] is True and
                    source['decoder_or_encoder_used'] is False and source['layers'] == source['depth'] == 1 and
                    descriptor['format'] in (81, 95), 'source differs from pinned standard ASTC cache/shape')
            texture = Path(specification['texture']).read_bytes()
            require(bulk.digest(texture) == source['source_texture']['sha256'], 'original ASTC source changed')
            bulk.source_index_matches(source_index, name, source['source_texture'])
            bulk.read_indexed(astc_inputs, manifest[name], source['source_texture'], texture)
            require(texture[:4] == b'<DE(' and texture[4] == descriptor['format'] and
                    struct.unpack_from('<II', texture, 8) == (descriptor['width'], descriptor['height']) and
                    struct.unpack_from('<Q', texture, 19)[0] == descriptor['layoutData'],
                    'control header differs from original ASTC descriptor')
            require(quality[name]['width'] == source['width'] and quality[name]['height'] == source['height'],
                    'source quality dimensions differ')
            before = old[old_map[name]]
            original = bulk.touch_layout._read_entry(str(raw), manifest[name])
            require(original[:4] == b'<DE(' and original[4] == before['format'], 'original BC texture/cache differs')
            changed = control_descriptor(before, descriptor)
            patched[old_map[name]] = descriptor
            result = bulk.encode_one(stream, texture, manifest[name][8], location)
            updated[name] = result
            key, encoded, decoded = result
            index.write(f'{name}\t{key.hex()}\t{encoded}\t{decoded}\n')
            bulk.read_indexed(inputs, manifest[name], dict(ekey=key.hex(), encoded_bytes=encoded,
                                encoding=manifest[name][8]), texture)
            proofs.append(dict(logical=name, original_BC_sha256=bulk.digest(original),
                ASTC_source_sha256=bulk.digest(texture), original_ASTC_PSNR=quality[name]['PSNR'],
                original_NVN_layout_preserved=True, DMN2_marker=False, changed_descriptor_fields=changed,
                source_and_control_CASC_roundtrip=True))
            bytes_total += len(texture)
    selected = set(names)
    require(all(left == right for left, right in zip(old, patched) if left['filename'].lower() not in selected),
            'unselected control cache entry changed')
    files = out / 'files'
    files.mkdir()
    cache_path = files / 'cache'
    cache_path.write_bytes(bulk.canonical(patched))
    output, proof = bulk.finalize_overlay(out, inputs, archive_name, archive, cache_path, updated,
                                         positions, manifest, ui_proof, h)
    for module in list(sys.modules.values()):
        path = getattr(module, '__file__', None)
        if path and Path(path).suffix == '.py' and Path(path).resolve().is_relative_to(ROOT):
            h.file(path)
    fingerprints = h.finish()
    result = dict(schema=1, complete=True, kind='matched-selected-standard-NVN-ASTC4-control',
        source_revision=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        source_fingerprints=fingerprints, source_pack_manifest_sha256=a.source_pack_manifest_sha256,
        selected_logicals_sha256=bulk.digest(bulk.canonical(names)), entry_count=len(names), entry_proofs=proofs,
        control_texture_bytes=bytes_total, cache_entries=len(old), unchanged_cache_entries=len(old) - len(names),
        changed_VFS_entries=names + [bulk.CACHE], original_TVFS_proof=proof, upstream_UI_preserved=True,
        all_CFT_offsets_unchanged=True, native_table_present=False, DMN2_markers=False,
        unselected_encoding='original BC unchanged; identical to matched native candidate',
        exact_generated_overlay_files={str(path.relative_to(output)): bulk.digest_file(path)
            for path in output.rglob('*') if path.is_file() and not path.is_symlink()},
        source_hashing=dict(unique_files=len(fingerprints), bytes=h.hashed_bytes,
                            large_archive_hashed_once_per_unique_real_path=True),
        device_access=False, game_binary_modified=False, game_acceptance=False, deploy_allowed=False,
        pair_requirements=['same source_pack_manifest_sha256 and selected_logicals_sha256 as native composition',
            'same original BC/UI baseline; same runtime, driver, graphics options and fixed .6 3D scale in device A/B',
            'no FPS/RAM/bandwidth/heat inference from offline composition'])
    (out / 'composition.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['raw', 'ui-overlay', 'ui-proof', 'entries', 'source-pack-manifest', 'artifact-root', 'out']:
        parser.add_argument('--' + name, required=True, type=Path)
    for name in ['entries-sha256', 'source-pack-manifest-sha256']:
        parser.add_argument('--' + name, required=True)
    result = compose(parser.parse_args())
    print(json.dumps({key: result[key] for key in ['complete', 'kind', 'entry_count',
                        'selected_logicals_sha256', 'DMN2_markers', 'deploy_allowed']}))


if __name__ == '__main__':
    main()
