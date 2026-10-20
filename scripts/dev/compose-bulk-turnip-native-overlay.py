#!/usr/bin/env python3
"""Compose authenticated family-v2 native entries over original BC and upstream UI.

Offline only. Reuses the original packer, CASC encoder/reader and complete TVFS
proof. One texture is materialized at a time; only one final encoded archive is
kept. A one-material functional receipt never certifies new bulk shapes.
"""
import argparse
import base64
import csv
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'dm/tools'), str(ROOT / 'dm/ios/tools')]
import casc_overlay as casc
import touch_layout
import astc_compose as comp

CACHE = 'data/hd/texture_desc_cache.json'
ALLOWED_FIELDS = {'format', 'layoutData', 'thumbnailBase64'}


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


one = load_module(ROOT / 'scripts/dev/compose-one-turnip-native-overlay.py', 'compose_one_unchanged')
producer_gate = load_module(ROOT / 'scripts/dev/native_bulk_producer_gate.py', 'native_bulk_producer_gate')


class Fingerprints:
    """Hash each immutable source file once; reject mutation on every reuse/end."""
    def __init__(self):
        self.files = {}
        self.hashed_bytes = 0

    @staticmethod
    def stamp(path):
        s = path.stat()
        return s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns

    def file(self, path):
        path = Path(path).resolve(strict=True)
        stamp = self.stamp(path)
        if path not in self.files:
            with path.open('rb') as stream:
                value = hashlib.file_digest(stream, 'sha256').hexdigest()
            require(self.stamp(path) == stamp, 'source changed during hash: ' + str(path))
            self.files[path] = (stamp, value)
            self.hashed_bytes += stamp[2]
        require(self.files[path][0] == stamp, 'source changed since hash: ' + str(path))
        return self.files[path][1]

    def record(self, item):
        path = Path(item['path'])
        require(self.file(path) == item['sha256'], 'input fingerprint differs: ' + str(path))
        if 'bytes' in item:
            require(path.stat().st_size == item['bytes'], 'input byte count differs')
        return path.resolve()

    def finish(self):
        for path, (stamp, _) in self.files.items():
            require(self.stamp(path) == stamp, 'source changed before publication: ' + str(path))
        return {str(path): value for path, (_, value) in self.files.items()}


def normalized(name):
    require(isinstance(name, str) and name == casc.norm_path(name) and name.startswith('data/') and
            not any(c in name for c in '\\\t\r\n') and '..' not in Path(name).parts,
            'unsafe/noncanonical logical path')
    return name


def unique_entries(entries, allow_original_case=False):
    require(isinstance(entries, list) and 1 <= len(entries) <= 65536, 'empty/oversized entries')
    names = [normalized(e['logical'].lower() if allow_original_case else e['logical']) for e in entries]
    require(len(set(names)) == len(names), 'duplicate logical entries')
    return names


def authenticate_table(table, record, details=None):
    count = len(record['entries'])
    require(len(table) == 160 + 1056 * count and struct.unpack_from('<8sIIIIQ', table) ==
            (b'DMNTBL2\0', 2, 160, count, 1040, len(table)), 'family-v2 table shape')
    value = bytearray(table)
    value[128:160] = bytes(32)
    require(hashlib.sha256(value).digest() == table[128:160], 'table digest mismatch')
    require(table[32:64].hex() == record['pack_content_sha256'] and
            table[64:96].hex() == record['driver_binary_sha256'] and
            table[96:128].hex() == record['producer_source_sha256'], 'table identities differ')
    content = {'entries': record['entries'], 'driver_sha256': record['driver_binary_sha256'],
               'producer_source_sha256': record['producer_source_sha256']}
    require(digest(canonical(content)) == record['pack_content_sha256'], 'native pack content digest')
    if details is not None:
        require(details == record['entries'], 'reproduced entries differ')


def receipt_gate(path, expected, hashes):
    require(hashes.file(path) == expected, 'root functional receipt changed')
    r = json.loads(path.read_text())
    require(r['status'] == 'REAL_GAME_ONE_NATIVE_TEXTURE_FUNCTIONAL_ACCEPTED_NOT_BULK' and
            r['bulk_assets_accepted'] is False and r['production_promotion'] is False and
            r['actual_fragment_read']['recorded_draw_before_capture'] is True and
            r['identity']['same_live_shared_allocation_TRANSFER_and_FRAGMENT'] is True and
            r['restoration']['stopped_and_original_all_six_directories_APK_chain'] is True,
            'root one-material functional receipt required')
    for key in ['restoration', 'supplemental_official_tool_analysis']:
        hashes.record(r[key])
    for item in r['original_failed_controller_and_debugger_records']:
        hashes.record(item)
    for item in r['visual_review']['screenshots']:
        require(hashes.file(item['local']) == item['sha256'], 'receipt visual evidence changed')
    return r


def source_gate(path, expected, hashes, artifact_root):
    require(hashes.file(path) == expected, 'source-pack manifest changed')
    value = json.loads(path.read_text())
    require(value['schema'] == 1 and value['complete'] is True, 'partial source pack')
    names = unique_entries(value['entries'])
    context = value['source_context']
    for key in ['original_build_manifest', 'source_index', 'NVN_layout_library']:
        hashes.record(context[key])
    for item in context['tool_inputs']:
        hashes.record(item)
    for item in context['verified_archives'].values():
        hashes.record(item['archive'])
        for key in ['manifest', 'composition']:
            if key in item:
                hashes.record(item[key])
    provenance = context['full_astc_provenance']
    hashes.record(provenance)
    audit = json.loads(Path(provenance['path']).read_text())
    verified = audit['source_and_artifact_verification']
    require(verified and all(x['matches'] is True for x in verified) and
            audit['pack_verification']['matches'] is True, 'full ASTC provenance not verified')
    by_name = {}
    for item in verified + [audit['pack_verification']]:
        reported = Path(item['path'])
        historical = provenance.get('historical_tool_files', {}).get(str(reported))
        if historical is not None:
            require(str(reported) in {'dm/tools/astc_pack.py', 'dm/tools/casc_overlay.py'} and
                    historical['sha256'] == item['sha256'], 'historical tool does not match original audit')
            p = hashes.record(historical)
        else:
            p = reported if reported.is_absolute() else artifact_root / reported
        require(hashes.file(p) == item['sha256'], 'full ASTC source/quality pin differs: ' + str(p))
        by_name[reported.name] = p.resolve()
    require({'pack.json', 'pack_index.tsv', 'quality.tsv', 'repl.tsv'} <= by_name.keys(), 'full ASTC quality/index pins missing')
    require(Path(context['source_index']['path']).resolve() == by_name['pack_index.tsv'],
            'selected source index is outside full ASTC provenance')
    quality = {}
    with by_name['quality.tsv'].open() as stream:
        rows = csv.reader(stream, delimiter='\t')
        next(rows)
        for row in rows:
            require(len(row) == 6 and row[0] not in quality, 'duplicate/malformed original quality row')
            score = float(row[5])
            require(math.isfinite(score) and score > 0, 'invalid original quality result')
            quality[row[0]] = {'PSNR': score, 'width': int(row[2]), 'height': int(row[3])}
    require(set(names) <= quality.keys(), 'selected source lacks original ASTC quality record')
    selection = value['selection']
    require(selection['kind'] == 'act1env-and-characters-excluding-declared-terrain-array' and
            set(selection.get('formats', selection.get('format', []))) == {81, 95}, 'unsupported initial bulk selection')
    hashes.record(selection['array_closure'])
    closure = json.loads(Path(selection['array_closure']['path']).read_text())
    closure = closure.get('array_format_closure', closure)
    require(closure.get('groups'), 'original declared array groups missing')
    array_members = {normalized(name) for group in closure['groups'] for name in group['members']}
    require(not set(names) & array_members, 'declared terrain-array member in native single-layer selection')
    require(all(name.startswith(('data/hd/character/', 'data/hd/vfx/meshes/character/')) or
                (name.startswith('data/hd/env/') and '/act1/' in name) for name in names),
            'selection outside initial Act1 environment/character scope')
    replacements = casc.read_replacements(by_name['repl.tsv'])
    require(CACHE in replacements and hashes.file(replacements[CACHE]) == context['source_cache']['sha256'],
            'complete ASTC cache differs from pinned original replacement declaration')
    return value, quality, replacements[CACHE]


def merge_descriptor(before, astc, native):
    require({k: v for k, v in astc.items() if k != 'layoutData'} ==
            {k: v for k, v in native.items() if k != 'layoutData'}, 'packer changed ASTC thumbnail/metadata')
    changed = {key for key in before.keys() | native.keys() if before.get(key) != native.get(key)}
    require(changed <= ALLOWED_FIELDS and 'layoutData' in changed and
            (before['format'], native['format']) in {(61, 95), (62, 81)},
            'unsupported BC-to-native descriptor transformation')
    require(before.get('thumbnailMip') == native.get('thumbnailMip') and
            native.get('thumbnailMip', 0) > 0, 'independent original thumbnail tail required')
    if native.get('thumbnailBase64'):
        require(base64.b64decode(native['thumbnailBase64'], validate=True), 'invalid matching ASTC thumbnail')
    return sorted(changed)


def source_index_matches(index, logical, info):
    require(info['logical'] == logical and logical in index, 'source logical differs from pinned index')
    key, encoded, decoded = index[logical]
    require((info['ekey'], info['encoded_bytes'], info['decoded_bytes']) ==
            (key.hex(), encoded, decoded), 'source CFT differs from pinned source index')


def read_indexed(folder, original_row, info, expected):
    row = list(original_row)
    key = bytes.fromhex(info['ekey'])
    row[2:8] = [str(key[9]), str(key[10]), hashlib.md5(expected).hexdigest(),
                str(len(expected)), key.hex(), str(info['encoded_bytes'])]
    row[8] = info['encoding']
    require(touch_layout._read_entry(str(folder), row) == expected, 'original CASC source membership differs')



def encode_one(stream, content, encoding, location):
    encoded = casc.encode_by_espec(content, encoding)
    offset = stream.tell()
    require(offset + len(encoded) < 1 << 40, 'CASC archive offset exceeds original key layout')
    key = casc.ekey_for(encoded, *location, offset)
    stream.write(encoded)
    stream.flush()
    return key, len(encoded), len(content)


def original_baseline(raw, ui, ui_proof_path, names, h):
    ui_proof = json.loads(ui_proof_path.read_text())
    h.file(ui_proof_path)
    require(Path(ui_proof['base']).resolve() == raw and Path(ui_proof['overlay']).resolve() == ui and
            ui_proof['changed_texture_or_cache_entries'] == [] and ui_proof['original_filetable_prefix_identical'], 'original BC/upstream UI proof required')
    for item in ui_proof['overlay_files']:
        require(h.file(ui / item['relative']) == item['sha256'], 'upstream UI baseline changed')
    manifest = casc.read_manifest(raw)
    require(CACHE in manifest and all(name in manifest for name in names), 'selected original VFS path missing')
    cache_bytes = touch_layout._read_entry(str(raw), manifest[CACHE])
    require(digest(cache_bytes) == ui_proof['cache_original_sha256'], 'original BC cache differs')
    old = json.loads(cache_bytes)
    old_map = {normalized(d['filename'].lower()): i for i, d in enumerate(old)}
    require(len(old_map) == len(old) and set(names) <= old_map.keys(), 'duplicate/missing original descriptor')
    baseline = bytes(casc.load_vfs1(ui))
    original = bytes(casc.load_vfs1(raw))
    oo, osize = struct.unpack_from('>II', original, 28)
    bo, bsize = struct.unpack_from('>II', baseline, 28)
    positions = {}
    for name in names + [CACHE]:
        op = casc.cft_position(original, oo, oo + osize, manifest, name)
        bp = casc.cft_position(baseline, bo, bo + bsize, manifest, name)
        require(op - oo == bp - bo and original[op:op + 25] == baseline[bp:bp + 25], 'UI changed selected texture/cache CFT')
        positions[name] = bp
    return ui_proof, manifest, old, old_map, baseline, positions


def finalize_overlay(out, inputs, archive_name, archive, cache_path, updated, positions,
                     manifest, ui_proof, h):
    output = out / 'romfs_overlay'
    casc.make_overlay(inputs, output, {CACHE: cache_path}, updated)
    # Only the final native archive retains payload bytes. Hardlink then remove
    # the staging pathname; no second payload copy or regeneration is retained.
    final_archive = output / 'data' / archive_name
    require(final_archive.is_symlink(), 'prebuilt archive reference unexpectedly materialized')
    final_archive.unlink()
    os.link(archive, final_archive)
    archive.unlink()
    (inputs / 'data' / archive_name).unlink()
    (inputs / 'data' / archive_name).symlink_to(final_archive)
    final = bytes(casc.load_vfs1(output))
    cp = positions[CACHE]
    cache_key = final[cp:cp + 16]
    updated[CACHE] = (cache_key, struct.unpack_from('>I', final, cp + 16)[0], struct.unpack_from('>I', final, cp + 21)[0])
    for name, entry in updated.items():
        pos = positions[name]
        require(final[pos:pos + 16] == entry[0] and struct.unpack_from('>I', final, pos + 16)[0] == entry[1] and
                struct.unpack_from('>I', final, pos + 21)[0] == entry[2], 'final native/cache CFT differs')
    one.decoded(output, manifest[CACHE], final[cp:cp + 25], cache_path.read_bytes())
    proof = comp.prove_tvfs(inputs, output, updated)
    for item in ui_proof['overlay_files']:
        if item['relative'] != 'data/.build.config':
            require(h.file(output / item['relative']) == item['sha256'], 'upstream UI archive/sidecar changed')
    return output, proof


def compose(a):
    out, raw, ui, pack = [p.resolve() for p in [a.out, a.raw, a.ui_overlay, a.native_pack]]
    require(not out.exists(), 'output must be new; partial composition is not resumed')
    require(not subprocess.check_output(['git', '-C', str(ROOT), 'diff', 'HEAD', '--name-only'], text=True).strip(),
            'tracked tool source must be frozen')
    h = Fingerprints()
    acceptance = receipt_gate(a.acceptance, a.acceptance_sha256, h)
    new_producer = producer_gate.source_ready_gate(a.producer_source_ready, a.producer_source_ready_sha256,
        a.producer_source_sha256, acceptance, h)
    prepared, quality, source_cache_path = source_gate(a.source_pack_manifest, a.source_pack_manifest_sha256, h, a.artifact_root.resolve())
    context = prepared['source_context']
    require(new_producer['prepared']['manifest']['sha256'] == a.source_pack_manifest_sha256,
            'new producer source-ready prepared input differs')
    require(Path(context['original_build_manifest']['path']).resolve() == raw / 'data/build_manifest.psv', 'original BC build differs')
    record = json.loads((pack / 'native-pack.json').read_text())
    h.file(pack / 'native-pack.json')
    names = unique_entries(record['entries'], allow_original_case=True)
    require(record['complete'] is True and record['family_protocol'] == 2 and
            names == [e['logical'] for e in prepared['entries']], 'native/source pack selection/order differs')
    require(h.file(a.packer) == record['tool_sha256'], 'original family packer changed')
    require(h.file(a.entries) == a.entries_sha256, 'ordered packer specifications changed')
    specs = json.loads(a.entries.read_text())
    require(isinstance(specs, list) and len(specs) == len(names), 'incomplete ordered entries')
    driver_manifest = json.loads(a.driver_manifest.read_text())
    require(h.file(a.driver_manifest) == record['driver_build_record_sha256'], 'driver manifest differs')
    driver = h.record(driver_manifest['driver'])
    require(h.file(driver) == record['driver_binary_sha256'] == acceptance['driver_sha256'], 'driver differs from real-game functional driver')
    require(record['producer_source_sha256'] == a.producer_source_sha256, 'native producer differs from explicit new source pin')
    uuid = acceptance['identity']['actual_recipe_UUID_payload_input_equal_table']
    require(record['identity'] == ''.join(uuid[k] for k in ['layout_uuid', 'device_uuid', 'driver_uuid']), 'actual driver three UUIDs differ')
    table = (pack / 'native-layouts.bin').read_bytes()
    require(h.file(pack / 'native-layouts.bin') == record['table_sha256'], 'native table changed')
    authenticate_table(table, record)
    packer = load_module(a.packer, 'existing_family_packer')
    require(h.file(a.driver_query) == record['actual_driver_query_sha256'], 'actual producer query changed')
    queried, queried_producer = packer.queried_identity(a.driver_query)
    require(queried.hex() == record['identity'] and queried_producer.hex() == record['producer_source_sha256'], 'actual producer query identity differs')
    actual_coverage = producer_gate.actual_batches_gate(a.actual_batch_receipts, a.actual_batch_receipts_sha256,
        new_producer, a.producer_source_sha256, prepared, a.source_pack_manifest_sha256, acceptance, a.driver_query, h)
    ui_proof, manifest, old, old_map, baseline, positions = original_baseline(raw, ui, a.ui_proof, names, h)
    out.mkdir()
    inputs = out / 'inputs'
    one.link_inputs(raw, ui, inputs)
    require(bytes(casc.load_vfs1(inputs)) == baseline, 'layered upstream UI input differs')
    astc_inputs = out / 'astc-inputs'
    (astc_inputs / 'data').mkdir(parents=True)
    for name, item in context['verified_archives'].items():
        require(name == Path(name).name, 'unsafe source archive name')
        (astc_inputs / 'data' / name).symlink_to(Path(item['archive']['path']).resolve())
    source_index = casc.read_prebuilt(Path(context['source_index']['path']))
    source_index_matches(source_index, CACHE, context['source_cache'])
    source_cache_bytes = source_cache_path.read_bytes()
    read_indexed(astc_inputs, manifest[CACHE], context['source_cache'], source_cache_bytes)
    source_cache = json.loads(source_cache_bytes)
    astc_descriptors = {normalized(d['filename'].lower()): d for d in source_cache}
    require(len(astc_descriptors) == len(source_cache), 'duplicate complete ASTC cache entry')
    location = casc.available_location(inputs / 'data')
    archive_name = casc.data_filename(*location)
    prebuilt = out / 'prebuilt'
    prebuilt.mkdir()
    archive = prebuilt / archive_name
    (inputs / 'data' / archive_name).symlink_to(archive)
    selected = set(names)
    patched = list(old)
    details, updated, entry_proofs = [], {}, []
    with archive.open('wb') as stream, (prebuilt / 'pack_index.tsv').open('w') as index:
        index.write('# pack\t' + archive_name + '\n')
        for number, (name, specification, prepared_entry) in enumerate(zip(names, specs, prepared['entries']), 1):
            require(prepared_entry['id'] == digest(name.encode()), 'prepared logical ID differs')
            require(Path(specification['baked']).resolve() == Path(actual_coverage[prepared_entry['id']]['baked']),
                    'packer baked directory is not its actual batch output')
            for key in ['source', 'texture', 'input', 'prepare_report']:
                h.record(prepared_entry[key])
            require(Path(specification['source']).resolve() == Path(prepared_entry['source']['path']).resolve() and
                    Path(specification['texture']).resolve() == Path(prepared_entry['texture']['path']).resolve(), 'packer source declaration differs')
            source = json.loads(Path(specification['source']).read_text())
            require(source['original_build_manifest'] == context['original_build_manifest'] and
                    source['source_index'] == context['source_index'] and
                    source['verified_archives'] == context['verified_archives'] and
                    source['source_cache'] == context['source_cache'], 'per-entry source provenance differs')
            require(source['tool_inputs'] == context['tool_inputs'] and source['NVN_layout_library'] == context['NVN_layout_library'], 'per-entry original layout tools differ')
            require(source['source_descriptor'] == astc_descriptors[name], 'source descriptor outside pinned complete ASTC cache')
            require(quality[name]['width'] == source['width'] and quality[name]['height'] == source['height'], 'source quality dimensions differ')
            require(source['layout_roundtrip_useful_blocks_exact'] is True and source['decoder_or_encoder_used'] is False and
                    source['depth'] == source['layers'] == 1 and source['game_format'] in [81, 95], 'unsupported source shape/protocol')
            require(Path(specification['source']).with_name('resource-input.bin').resolve() == Path(prepared_entry['input']['path']).resolve(), 'linear input differs')
            for leaf in ['native-resource-layout.bin', 'native-resource.bin', 'native-resource.json']:
                h.file(Path(specification['baked']) / leaf)
            wire_entry, texture, descriptor, detail, identity = packer.load_entry(specification, number)
            require(detail == record['entries'][number - 1] and identity.hex() == record['identity'], 'packer reproduction differs')
            require(wire_entry == table[160 + (number - 1) * 1040:160 + number * 1040], 'table entry is not reproduced producer entry')
            family = struct.pack('<QII', detail['original_layout_data'], detail['thumbnail_mip'], 0)
            require(family == table[160 + len(names) * 1040 + (number - 1) * 16:160 + len(names) * 1040 + number * 16], 'table original-tail family differs')
            require(h.file(pack / detail['logical']) == detail['texture_sha256'] and digest(texture) == detail['texture_sha256'], 'packaged texture differs')
            astc = Path(specification['texture']).read_bytes()
            require(digest(astc) == source['source_texture']['sha256'], 'ASTC source differs')
            source_index_matches(source_index, name, source['source_texture'])
            read_indexed(astc_inputs, manifest[name], source['source_texture'], astc)
            original_texture = touch_layout._read_entry(str(raw), manifest[name])
            require(len(original_texture) >= 36 and original_texture[:4] == b'<DE(' and
                    original_texture[4] == old[old_map[name]]['format'], 'original BC texture/cache disagreement')
            changed = merge_descriptor(old[old_map[name]], source['source_descriptor'], descriptor)
            patched[old_map[name]] = descriptor
            row = manifest[name]
            result = encode_one(stream, texture, row[8], location)
            updated[name] = result
            key, encoded, decoded = result
            index.write(f'{name}\t{key.hex()}\t{encoded}\t{decoded}\n')
            read_indexed(inputs, row, {'ekey': key.hex(), 'encoded_bytes': encoded, 'encoding': row[8]}, texture)
            details.append(detail)
            entry_proofs.append({'id': number, 'logical': name, 'original_BC_sha256': digest(original_texture),
                                 'ASTC_source_sha256': digest(astc), 'original_ASTC_PSNR': quality[name]['PSNR'],
                                 'changed_descriptor_fields': changed, 'native_bytes': detail['image_size'],
                                 'packer_reproduced': True, 'source_CASC_roundtrip': True,
                                 'native_CASC_roundtrip': True, 'real_game_shape_accepted': False})
            del texture, astc, original_texture, wire_entry
    authenticate_table(table, record, details)
    require(all(a == b for a, b in zip(old, patched) if a['filename'].lower() not in selected), 'unselected cache entry changed')
    files = out / 'files'
    files.mkdir()
    cache_path = files / 'cache'
    cache_path.write_bytes(canonical(patched))
    (files / 'native-layouts.bin').write_bytes(table)
    output, proof = finalize_overlay(out, inputs, archive_name, archive, cache_path, updated, positions, manifest, ui_proof, h)
    for module in list(sys.modules.values()):
        path = getattr(module, '__file__', None)
        if path and Path(path).suffix == '.py' and Path(path).resolve().is_relative_to(ROOT):
            h.file(path)
    source_fingerprints = h.finish()
    generated = {str(path.relative_to(output)): digest_file(path) for path in output.rglob('*')
                 if path.is_file() and not path.is_symlink()}
    result = {'schema': 1, 'complete': True, 'source_revision': subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'source_fingerprints': source_fingerprints, 'source_hashing': {'unique_files': len(source_fingerprints),
        'bytes': h.hashed_bytes, 'large_archive_hashed_once_per_unique_real_path': True,
        'stat_identity_rechecked_before_publication': True},
        'driver_sha256': record['driver_binary_sha256'], 'driver_identity': record['identity'],
        'producer_source_sha256': record['producer_source_sha256'],
        'native_table_sha256': digest(table), 'native_pack_content_sha256': record['pack_content_sha256'],
        'source_pack_manifest_sha256': a.source_pack_manifest_sha256,
        'selected_logicals_sha256': digest(canonical(names)),
        'one_material_functional_receipt_sha256': a.acceptance_sha256,
        'producer_identity_lineage': {'legacy_one_material_producer': acceptance['identity']['producer_sha256'],
            'legacy_receipt_scope': 'driver/three UUIDs/protocol and one shape only; not new producer game acceptance',
            'current_producer': a.producer_source_sha256,
            'current_source_ready_sha256': a.producer_source_ready_sha256,
            'actual_batch_receipts_sha256': a.actual_batch_receipts_sha256,
            'actual_entries_covered': len(actual_coverage),
            'current_driver_query': str(a.driver_query.resolve()),
            'new_producer_game_accepted': False},
        'entry_count': len(names), 'entry_proofs': entry_proofs, 'native_payload_bytes': sum(d['image_size'] for d in details),
        'cache_entries': len(old), 'unchanged_cache_entries': len(old) - len(names),
        'changed_VFS_entries': names + [CACHE], 'unchanged_VFS_entries': one.vfs_path_count(baseline) - len(names) - 1,
        'original_TVFS_proof': proof, 'all_CFT_offsets_unchanged': True, 'original_CASC_roundtrip': True,
        'upstream_UI_preserved': True, 'exact_generated_overlay_files': generated,
        'streaming_composition': {'one_texture_at_a_time': True, 'second_batch_payload_copy_retained': False,
                                  'original_CASC_encoder_and_prebuilt_TVFS': True},
        'driver_specific_payload': True, 'game_binary_modified': False, 'device_access': False,
        'game_acceptance': False, 'deploy_allowed': False, 'new_bulk_shapes_actual_game_accepted': False,
        'remaining': ['separately authorized bulk actual-game Builder/storage/streaming/visual validation',
                      'performance, DRAM bandwidth, sustained heat and 6GB capacity validation']}
    (out / 'composition.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def digest_file(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['raw', 'ui-overlay', 'ui-proof', 'native-pack', 'entries', 'packer', 'driver-manifest',
                 'source-pack-manifest', 'acceptance', 'artifact-root', 'driver-query', 'producer-source-ready', 'actual-batch-receipts', 'out']:
        parser.add_argument('--' + name, required=True, type=Path)
    for name in ['entries-sha256', 'source-pack-manifest-sha256', 'acceptance-sha256',
                 'producer-source-sha256', 'producer-source-ready-sha256', 'actual-batch-receipts-sha256']:
        parser.add_argument('--' + name, required=True)
    result = compose(parser.parse_args())
    print(json.dumps({key: result[key] for key in ['complete', 'entry_count', 'native_payload_bytes',
                                                  'native_table_sha256', 'deploy_allowed']}))


if __name__ == '__main__':
    main()
