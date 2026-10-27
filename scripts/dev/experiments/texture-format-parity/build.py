#!/usr/bin/env python3
"""Compose existing pre-native texture bytes with the original CASC tools; offline only."""
import argparse
import base64
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'dm/tools'), str(ROOT / 'dm/ios/tools')]
import casc_overlay as casc
import astc_compose as composition
import astc_repack as repack
import astc_array_groups as arrays
import touch_layout
CACHE = 'data/hd/texture_desc_cache.json'
FORMATS = {95: (0x79, 157, 4), 81: (0x87, 158, 4), 99: (0x7d, 165, 6), 85: (0x8b, 166, 6)}


def require(value, reason):
    if not value:
        raise ValueError(reason)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def name(value):
    require(isinstance(value, str) and value == casc.norm_path(value) and value.startswith('data/') and
            '..' not in Path(value).parts and not any(x in value for x in '\\\t\r\n'), 'unsafe logical name')
    return value


class Inputs:
    def __init__(self):
        self.files = {}

    def pin(self, path, expected=None):
        path = Path(path).resolve(strict=True)
        st = path.stat()
        stamp = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)
        if path not in self.files:
            with path.open('rb') as f:
                value = hashlib.file_digest(f, 'sha256').hexdigest()
            self.files[path] = (stamp, value)
        require(self.files[path][0] == stamp, 'input changed: ' + str(path))
        value = self.files[path][1]
        require(expected is None or expected == value, 'input hash mismatch: ' + str(path))
        return value

    def finish(self):
        for p in self.files:
            self.pin(p)
        return {str(p): value for p, (_, value) in self.files.items()}


def load_pinned(item, inputs):
    p = Path(item['path'])
    inputs.pin(p, item['sha256'])
    return json.loads(p.read_text())


def descriptor_map(cache):
    require(isinstance(cache, list), 'complete descriptor cache must be a list')
    result = {}
    for i, row in enumerate(cache):
        n = name(row['filename'].lower())
        require(n not in result, 'duplicate descriptor')
        result[n] = i
    return result


def ordinary_descriptor(native, source, detail):
    descriptor = source['source_descriptor']
    require(native == dict(descriptor, layoutData=int.from_bytes(bytes.fromhex(detail['layout8']), 'little')),
            'native cache differs beyond declared family marker')
    require(descriptor['format'] in FORMATS and descriptor['type'] == 2 and descriptor['depth'] == 1 and
            descriptor['layoutType'] == 3 and 0 < descriptor['levels'] <= 16 and
            0 < descriptor['width'] <= 4096 and 0 < descriptor['height'] <= 4096, 'unsupported descriptor schema/shape')
    require(struct.pack('<Q', descriptor['layoutData'])[:3] != b'DMN', 'ordinary source still contains DMN marker')
    thumb = descriptor.get('thumbnailBase64')
    if thumb:
        require(0 < descriptor.get('thumbnailMip', 0) < descriptor['levels'], 'thumbnail family is not distinguishable')
        require(base64.b64decode(thumb, validate=True), 'empty thumbnail')
    return descriptor


def validate_texture(data, descriptor, source):
    nvn, vk, block = FORMATS[descriptor['format']]
    require((source['NVN_format'], source['VkFormat'], source['block']) == (nvn, vk, [block, block]), 'source format mismatch')
    require(source['layout_roundtrip_useful_blocks_exact'] is True and source['decoder_or_encoder_used'] is False,
            'source layout provenance is not exact')
    _, first, desc, _ = repack.full_texture(data, descriptor, nvn_format=nvn)
    require(len(source['mips']) == desc.levels, 'source mip declaration incomplete')
    for level, row in enumerate(source['mips']):
        raw = repack.deswizzle(desc, data[first:], level, block)
        require(row['level'] == level and len(raw) == row['linear_bytes'] and digest(raw) == row['sha256'],
                'original NVN mip useful blocks differ from pre-native source')
    return desc.levels


def link_baseline(raw, ui, output):
    # Existing CASC link_tree handles unchanged files. Overlay the upstream UI
    # into the private work tree; never modify or relink the original directory.
    casc.link_tree(raw, output)
    for p in sorted(ui.rglob('*')):
        if not p.is_file():
            continue
        target = output / p.relative_to(ui)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_symlink():
            target.unlink()
        require(not target.exists(), 'baseline overlay collision')
        target.symlink_to(p.resolve())


def decoded_at(folder, original, info, expected):
    key, encoded, decoded = info
    row = list(original)
    row[2:8] = [str(key[9]), str(key[10]), hashlib.md5(expected).hexdigest(), str(decoded), key.hex(), str(encoded)]
    require(decoded == len(expected) and touch_layout._read_entry(str(folder), row) == expected,
            'original CASC reader roundtrip mismatch')


def build(config_path, output):
    require(not output.exists(), 'output must be new; partial outputs are not resumed')
    h = Inputs(); h.pin(config_path)
    cfg = json.loads(config_path.read_text()); require(cfg['schema'] == 1, 'unknown build schema')
    raw, ui = Path(cfg['raw']).resolve(), Path(cfg['upstream_overlay']).resolve()
    for p in [Path(__file__), Path(casc.__file__), Path(composition.__file__), Path(repack.__file__), Path(arrays.__file__), Path(touch_layout.__file__), ROOT / 'dm/ios/tools/romfs_art.py', ROOT / 'dm/gfx/nvn/nvn_tex.c', ROOT / 'dm/gfx/nvn/nvn_tex.h', ROOT / 'dm/gfx/tex/nvn_deswizzle.c', ROOT / 'dm/gfx/tex/nvn_deswizzle.h']:
        h.pin(p)
    h.pin(Path(repack.romfs_art.lib()._name))
    upstream = Path(cfg['upstream_source']).resolve()
    contract_path = upstream / 'dm/ios/tools/overlay_contract.py'
    spec = importlib.util.spec_from_file_location('upstream_overlay_contract', contract_path)
    contract = importlib.util.module_from_spec(spec); spec.loader.exec_module(contract)
    source_id = contract.source_fingerprint(upstream)
    require(source_id == cfg['upstream_overlay_source_sha256'], 'upstream UI generator source mismatch')
    ui_files = contract.verify_manifest(ui, source_id)
    for relative, expected in ui_files.items(): h.pin(ui / relative, expected)
    h.pin(ui / 'overlay.manifest')
    for relative in contract.SOURCE_INPUTS: h.pin(upstream / relative)
    entries = load_pinned(cfg['entries'], h)
    native = load_pinned(cfg['native_pack'], h)
    current_cache = load_pinned(cfg['native_cache'], h)
    closure = load_pinned(cfg['array_closure'], h)
    closure = closure.get('array_format_closure', closure)
    require(native['complete'] is True and native['pack_content_sha256'] == cfg['native_pack_content_sha256'] and
            len(entries) == len(native['entries']) == cfg['expected_selected'], 'current native selection identity mismatch')
    mapping = descriptor_map(current_cache)
    require(len(current_cache) == cfg['expected_cache_entries'], 'incomplete current cache')
    for p in sorted((raw / 'data').iterdir()):
        if p.is_file(): h.pin(p)
    manifest = casc.read_manifest(raw)
    original_cache = json.loads(touch_layout._read_entry(str(raw), manifest[CACHE]))
    require([x['filename'] for x in original_cache] == [x['filename'] for x in current_cache], 'cache ordering/names differ')
    selected, patched, source_files, proofs = {}, list(current_cache), {}, []
    counts = collections.Counter(); mip_count = 0; payload_bytes = 0
    for ordinal, (entry, detail) in enumerate(zip(entries, native['entries'])):
        n = name(detail['logical'].lower()); require(n not in selected and n in mapping and n in manifest, 'duplicate/missing selected logical')
        p = Path(entry['source']); h.pin(p, detail['source_declaration_sha256']); source = json.loads(p.read_text())
        for key in ['original_build_manifest', 'full_astc_provenance']:
            evidence = source[key]; h.pin(evidence['path'], evidence['sha256'])
        for evidence in source.get('qualified_ASTC6_source', {}).values():
            if isinstance(evidence, dict) and 'path' in evidence and 'sha256' in evidence:
                h.pin(evidence['path'], evidence['sha256'])
        t = Path(entry['texture']); h.pin(t, detail['source_texture_sha256'])
        data = t.read_bytes(); require(digest(data) == source['source_texture']['sha256'], 'ordinary source hash mismatch')
        descriptor = ordinary_descriptor(current_cache[mapping[n]], source, detail)
        require(name(descriptor['filename'].lower()) == n, 'logical/descriptor mismatch')
        mip_count += validate_texture(data, descriptor, source)
        require(all(original_cache[mapping[n]].get(k) == descriptor.get(k) for k in descriptor
                    if k not in {'format', 'layoutData', 'thumbnailBase64'}), 'ordinary descriptor changed unrelated fields')
        patched[mapping[n]] = descriptor; selected[n] = True; source_files[n] = t
        payload_bytes += len(data); counts[str(source['VkFormat'])] += 1
        if ordinal % 1000 == 0: print('Verified ordinary source', ordinal + 1, '/', len(entries), flush=True)
        proofs.append({'logical': n, 'source_declaration_sha256': h.pin(p), 'texture_sha256': h.pin(t),
                       'format': descriptor['format'], 'VkFormat': source['VkFormat'], 'levels': descriptor['levels']})
    require(all(a == b == c for a, b, c in zip(original_cache, current_cache, patched)
                if a['filename'].lower() not in selected), 'fallback outside native selection differs from original BC cache')
    before = {x['filename'].lower(): x for x in original_cache}; after = {x['filename'].lower(): x for x in patched}
    require(bool(closure.get('groups')), 'declared array census absent')
    closed, array_proof = arrays.close_selection(before, after, selected, closure['groups'], closure)
    require(closed == selected and not array_proof['preserved'], 'selection would split declared texture array')
    require(all(not struct.pack('<Q', row['layoutData'])[:3] == b'DMN' for row in patched), 'DMN marker leaked into ordinary cache')
    output.mkdir(parents=True); files = output / 'files'; files.mkdir()
    cache_path = files / 'texture_desc_cache.json'; cache_path.write_bytes(canonical(patched))
    source_files[CACHE] = cache_path
    replacements = files / 'replacements.tsv'; replacements.write_text(''.join(n + '\t' + str(p.resolve()) + '\n' for n, p in source_files.items()))
    baseline = output / 'baseline-romfs'; link_baseline(raw, ui, baseline)
    require(touch_layout._read_entry(str(baseline), manifest[CACHE]) == canonical(original_cache) or
            json.loads(touch_layout._read_entry(str(baseline), manifest[CACHE])) == original_cache, 'upstream UI changed original descriptor cache')
    location = casc.available_location(baseline / 'data'); pack = output / 'prebuilt'
    command = [sys.executable, '-X', 'cpu_count=2', str(Path(casc.__file__)), 'pack', str(baseline), str(pack), str(replacements), str(location[0]) + '-' + str(location[1])]
    with (output / 'pack.log').open('w') as f: subprocess.run(command, check=True, stdout=f, stderr=subprocess.STDOUT)
    index_path = pack / 'pack_index.tsv'; index = casc.read_prebuilt(index_path)
    require(set(index) == set(source_files), 'CASC index incomplete')
    archive_name = casc.data_filename(*location); (baseline / 'data' / archive_name).symlink_to((pack / archive_name).resolve())
    candidate_romfs = output / 'candidate-romfs'
    casc.make_overlay(baseline, candidate_romfs, {}, index)
    proof = composition.prove_tvfs(baseline, candidate_romfs, index)
    for n, p in source_files.items(): decoded_at(candidate_romfs, manifest[n], index[n], p.read_bytes())
    overlay = output / 'candidate-overlay'; shutil.copytree(ui, overlay)
    # Existing runtime manifest rejects links. Only emitted files are included;
    # unchanged 5.56GB raw archives remain in the original, read-only base.
    for p in (candidate_romfs / 'data').iterdir():
        if p.name == '.build.config' or not p.is_symlink() or p.name == archive_name:
            shutil.copy2(p.resolve(), overlay / 'data' / p.name)
    contract.write_manifest(overlay, source_id); actual = contract.verify_manifest(overlay, source_id)
    require(all(actual[k] == v for k, v in ui_files.items() if k != 'data/.build.config'), 'upstream UI/sidecar bytes changed')
    for p in [Path(__file__), Path(casc.__file__), Path(composition.__file__), Path(repack.__file__), Path(arrays.__file__), Path(touch_layout.__file__)]: h.pin(p)
    source_hashes = h.finish()
    artifacts = {str(p.relative_to(output)): {'bytes': p.stat().st_size, 'sha256': digest(p.read_bytes())}
                 for p in output.rglob('*') if p.is_file() and not p.is_symlink()}
    result = {'schema': 1, 'complete': True, 'offline_only': True, 'device_access': False, 'game_binary_modified': False,
              'texture_encoder_used': False, 'native_table_or_physical_payload_in_candidate': False,
              'upstream_UI_source_sha256': source_id, 'native_pack_content_sha256': native['pack_content_sha256'],
              'selected': len(selected), 'full_cache_entries': len(patched), 'fallback_entries_unchanged': len(patched) - len(selected),
              'selected_VkFormat_counts': dict(counts), 'source_texture_bytes': payload_bytes, 'validated_mips': mip_count,
              'array_format_closure': array_proof, 'TVFS_CFT_root_config_proof': proof,
              'all_selected_CASC_roundtrips_exact': True, 'upstream_UI_files_unchanged_except_config': True,
              'candidate_overlay': str(overlay.resolve()), 'candidate_overlay_bytes': sum((overlay / p).stat().st_size for p in actual),
              'candidate_manifest_sha256': digest((overlay / 'overlay.manifest').read_bytes()), 'commands': [command],
              'source_fingerprints': source_hashes, 'artifacts': artifacts,
              'runtime_acceptance': 'NOT_TESTED; original APK needs an explicit validated cold overlay selection; do not overwrite existing overlay or S2X'}
    write(output / 'selected-source-proofs.json', proofs)
    result['artifacts']['selected-source-proofs.json'] = {'bytes': (output / 'selected-source-proofs.json').stat().st_size, 'sha256': digest((output / 'selected-source-proofs.json').read_bytes())}
    write(output / 'composition.json', result)
    write(output / 'composition.sha256.json', {'composition.json': digest((output / 'composition.json').read_bytes())})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--config', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); result = build(a.config.resolve(), a.output.resolve())
    print(json.dumps({k: result[k] for k in ['complete', 'selected', 'fallback_entries_unchanged', 'source_texture_bytes', 'candidate_overlay_bytes', 'candidate_overlay']}, indent=2))


if __name__ == '__main__': main()
