#!/usr/bin/env python3
"""Compose one GPU-verified native family over an original-BC/UI overlay.

Only orchestrates the existing family packer and CASC implementation. Does not
deploy, patch a game executable, or reinterpret another driver's native bytes.
"""
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'dm/tools'), str(ROOT / 'dm/ios/tools')]
import casc_overlay as casc
import touch_layout

CACHE = 'data/hd/texture_desc_cache.json'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def vfs_entries(folder):
    data = casc.load_vfs1(folder)
    start, size = struct.unpack_from('>II', data, 12)
    cft, count = struct.unpack_from('>II', data, 28)
    require(cft + count <= len(data), 'invalid CFT bounds')
    result = {}
    for _, name, value, directory, _ in casc.walk_paths(data, start, start + size):
        if directory is None:
            logical = casc.norm_path(name.decode())
            require(logical not in result, 'duplicate VFS path')
            offset = cft + value
            require(offset + 25 <= cft + count, 'invalid CFT item')
            result[logical] = (value, bytes(data[offset:offset + 25]))
    return result


def decoded(folder, row, cft, expected):
    key = cft[:16]
    fields = list(row)
    fields[2:8] = [str(key[9]), str(key[10]), hashlib.md5(expected).hexdigest(),
                   str(struct.unpack_from('>I', cft, 21)[0]), key.hex(),
                   str(struct.unpack_from('>I', cft, 16)[0])]
    content = touch_layout._read_entry(str(folder), fields)
    require(content == expected, 'original CASC reader content mismatch: ' + row[9])
    return content


def link_inputs(raw, ui, output):
    """Layer existing UI files over original data, without copying archives."""
    output.mkdir()
    for entry in raw.iterdir():
        if entry.name != 'data':
            (output / entry.name).symlink_to(entry.resolve())
    (output / 'data').mkdir()
    for entry in (raw / 'data').iterdir():
        (output / 'data' / entry.name).symlink_to(entry.resolve())
    for entry in ui.iterdir():
        if entry.name == 'data':
            for item in entry.iterdir():
                target = output / 'data' / item.name
                if target.is_symlink():
                    target.unlink()
                require(not target.exists(), 'unexpected baseline data collision')
                target.symlink_to(item.resolve())
        else:
            target = output / entry.name
            if target.is_symlink():
                target.unlink()
            require(not target.exists(), 'unexpected baseline file collision')
            target.symlink_to(entry.resolve())


def compose(a):
    out, raw, ui, pack = a.out.resolve(), a.raw.resolve(), a.ui_overlay.resolve(), a.native_pack.resolve()
    require(not out.exists(), 'output must be new')
    require(not subprocess.check_output(['git', '-C', str(ROOT), 'diff', 'HEAD', '--name-only'], text=True).strip(),
            'tracked tool source must be frozen')
    acceptance = json.loads(a.acceptance.read_text())
    require(sha(a.acceptance) == a.acceptance_sha256, 'acceptance changed')
    require(acceptance['status'] == 'actual-ordinary-UID-real-one-material-original-NVN-family-passed',
            'actual original NVN family acceptance required')
    ui_proof = json.loads(a.ui_proof.read_text())
    require(Path(ui_proof['base']).resolve() == raw and Path(ui_proof['overlay']).resolve() == ui and
            ui_proof['changed_texture_or_cache_entries'] == [] and ui_proof['original_filetable_prefix_identical'],
            'original-BC upstream UI baseline proof required')
    for item in ui_proof['overlay_files']:
        require(sha(ui / item['relative']) == item['sha256'], 'UI baseline changed: ' + item['relative'])
    record = json.loads((pack / 'native-pack.json').read_text())
    require(record['complete'] and record['family_protocol'] == 2 and len(record['entries']) == 1,
            'one complete family-v2 entry required')
    entry = record['entries'][0]
    logical = casc.norm_path(entry['logical'])
    require(logical == entry['logical'] == acceptance['input']['logical_path'], 'logical source differs')
    require(record['driver_binary_sha256'] == acceptance['family']['identity']['driver_sha256'] == sha(a.driver),
            'native driver differs from actual GPU acceptance')
    require(record['identity'] == ''.join(acceptance['family']['identity'][key]
            for key in ('layout_UUID', 'device_UUID', 'driver_UUID')), 'native UUID tuple differs')
    require(record['producer_source_sha256'] == a.producer_source_sha256,
            'producer fingerprint differs')
    require(sha(pack / 'native-layouts.bin') == record['table_sha256'], 'native table changed')
    require(sha(pack / logical) == entry['texture_sha256'], 'native texture changed')
    evidence = acceptance['evidence']
    for path, expected in evidence.items():
        require(sha(path) == expected, 'actual acceptance evidence changed: ' + path)
    results = [json.loads(Path(path).read_text()) for path in evidence
               if path.endswith('/result.json')]
    actual = [r for r in results if r.get('stage') == 'actual-original-NVN-native-family-owned-passed']
    require(len(actual) == 1 and actual[0]['original_unchanged'] and not actual[0]['cleanup_errors'],
            'actual family transaction missing')
    resources = actual[0]['owned_resource_inputs']
    require(resources['resource4/native-layouts.bin'] == record['table_sha256'] and
            resources['resource4/native-resource.bin'] == entry['native_payload_sha256'],
            'table/payload not covered by actual GPU oracle')
    # Regenerate using the unchanged existing packer. Its original input/wire,
    # mip bounds, metadata hashes and matching ASTC thumbnail checks all remain.
    require(sha(a.packer) == record['tool_sha256'], 'original family packer changed')
    spec = json.loads(a.entries.read_text())
    require(isinstance(spec, list) and len(spec) == 1, 'one original packer input required')
    module = importlib.util.spec_from_file_location('original_native_packer', a.packer)
    packer = importlib.util.module_from_spec(module)
    module.loader.exec_module(packer)
    _, texture, descriptor, detail, _ = packer.load_entry(spec[0], 1)
    require(texture == (pack / logical).read_bytes() and detail == entry,
            'native pack does not reproduce from original producer inputs')
    require(digest(canonical(descriptor)) == entry['descriptor_sha256'], 'native descriptor changed')
    require(descriptor['format'] == 81 and descriptor['thumbnailMip'] > 0,
            'this accepted source is ASTC4 sRGB with an independent original tail')
    source = json.loads(Path(spec[0]['source']).read_text())
    require(source['source_texture']['sha256'] == entry['source_texture_sha256'] and
            sha(Path(spec[0]['source']).with_name('resource-input.bin')) ==
            acceptance['input']['verified_compact_input_sha256'], 'accepted control source differs')
    manifest = casc.read_manifest(raw)
    require(logical in manifest and CACHE in manifest, 'source entries missing')
    original_cache = touch_layout._read_entry(str(raw), manifest[CACHE])
    original_texture = touch_layout._read_entry(str(raw), manifest[logical])
    require(digest(original_cache) == acceptance['input']['original_cache_sha256'] and
            digest(original_texture) == acceptance['input']['original_texture_sha256'], 'original BC input differs')
    old = json.loads(original_cache)
    selected = [i for i, d in enumerate(old) if d['filename'].lower() == logical]
    require(len(selected) == 1, 'original descriptor must be unique')
    index = selected[0]
    before = old[index]
    # The existing accepted control converted BC3 to ASTC4, including its
    # thumbnail. Never put the old BC thumbnail under the new format header.
    changed_fields = {k for k in before.keys() | descriptor.keys() if before.get(k) != descriptor.get(k)}
    require(before['format'] == 62 and changed_fields == {'format', 'layoutData', 'thumbnailBase64'},
            'unsupported source descriptor transformation')
    require(before['thumbnailMip'] == descriptor['thumbnailMip'] and
            base64.b64decode(descriptor['thumbnailBase64'], validate=True), 'matching ASTC thumbnail required')
    patched = list(old)
    patched[index] = descriptor
    require(all(a == b for i, (a, b) in enumerate(zip(old, patched)) if i != index),
            'unrelated cache entry changed')
    baseline = vfs_entries(ui)
    originals = vfs_entries(raw)
    require(all(baseline[name] == originals[name] for name in (logical, CACHE)),
            'UI baseline already changed the selected texture/cache')
    out.mkdir()
    inputs = out / 'inputs'
    link_inputs(raw, ui, inputs)
    require(vfs_entries(inputs) == baseline, 'layered UI input differs from baseline VFS')
    files = out / 'files'
    files.mkdir()
    (files / 'texture').write_bytes(texture)
    (files / 'cache').write_bytes(canonical(patched))
    (files / 'native-layouts.bin').write_bytes((pack / 'native-layouts.bin').read_bytes())
    replacements = {logical: files / 'texture', CACHE: files / 'cache'}
    table = out / 'replacements.tsv'
    table.write_text(''.join(name + '\t' + str(path) + '\n' for name, path in replacements.items()))
    output = out / 'romfs_overlay'
    # Original CASC writer preserves the full UI tree and existing CFT order.
    casc.make_overlay(inputs, output, replacements)
    final = vfs_entries(output)
    require(final.keys() == baseline.keys(), 'VFS path tree changed')
    differences = [name for name in baseline if baseline[name] != final[name]]
    require(set(differences) == set(replacements), 'unrelated VFS entry changed')
    require(all(final[name][0] == baseline[name][0] for name in final), 'CFT offsets moved')
    for name, path in replacements.items():
        decoded(output, manifest[name], final[name][1], path.read_bytes())
    require(all(sha(output / item.name) == sha(item) for item in ui.iterdir() if item.is_file()),
            'upstream UI sidecar changed')
    source_paths = [Path(__file__), ROOT / 'dm/tools/casc_overlay.py', ROOT / 'dm/ios/tools/touch_layout.py',
                    a.packer, a.acceptance, a.ui_proof, pack / 'native-pack.json', a.entries,
                    raw / 'data/build_manifest.psv', ui / 'data/.build.config',
                    Path(spec[0]['source']), Path(spec[0]['texture'])]
    source_paths.extend(ui / item['relative'] for item in ui_proof['overlay_files'])
    generated = {str(p.relative_to(output)): sha(p) for p in output.rglob('*')
                 if p.is_file() and not p.is_symlink()}
    result = {'schema': 1, 'complete': True, 'source_revision': subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'source_fingerprints': {str(p): sha(p) for p in source_paths},
        'driver_sha256': sha(a.driver), 'producer_source_sha256': a.producer_source_sha256,
        'native_table_sha256': sha(files / 'native-layouts.bin'),
        'native_pack_content_sha256': record['pack_content_sha256'],
        'logical': logical, 'changed_descriptor_fields': sorted(changed_fields),
        'cache_entries': len(old), 'unchanged_cache_entries': len(old) - 1,
        'unchanged_VFS_entries': len(baseline) - 2, 'changed_VFS_entries': sorted(differences),
        'all_CFT_offsets_unchanged': True, 'original_CASC_roundtrip': True,
        'upstream_UI_preserved': True, 'native_payload_bytes': entry['image_size'],
        'exact_generated_overlay_files': generated, 'driver_specific_payload': True,
        'game_binary_modified': False, 'device_access': False, 'game_acceptance': False,
        'deploy_allowed': False, 'remaining': ['integrated native-window allocation GPU verification',
            'actual game Builder size/alignment/full-tail and streamed-storage acceptance']}
    (out / 'composition.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('raw', 'ui-overlay', 'ui-proof', 'native-pack', 'entries', 'packer', 'acceptance', 'driver', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    for name in ('acceptance-sha256', 'producer-source-sha256'):
        p.add_argument('--' + name, required=True)
    compose(p.parse_args())


if __name__ == '__main__':
    main()
