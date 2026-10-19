#!/usr/bin/env python3
"""Verify a completed ASTC6 batch and compose it over a verified full4/UI overlay.

No encoding, NVN library loading, UI generation, game patching or deployment.
Archive/TVFS production is performed only by the existing casc_overlay.py CLI.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ios/tools'))
import casc_overlay as casc
import touch_layout

CACHE = 'data/hd/texture_desc_cache.json'
PACK9 = '00-00000009.data'
ROOT10 = '00-0000000a.data'


def require(condition, message):
    if not condition:
        raise ValueError(message)


class Hashes:
    """Hash each immutable inode once, including hard-linked baseline archives."""
    def __init__(self):
        self.cache = {}

    def file(self, path):
        path = Path(path)
        before = path.stat()
        key = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
        if key not in self.cache:
            with path.open('rb') as stream:
                value = hashlib.file_digest(stream, 'sha256').hexdigest()
            after = path.stat()
            require((after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) == key,
                    'input changed while hashing: ' + str(path))
            self.cache[key] = value
        return self.cache[key]


def byte_sha(data):
    return hashlib.sha256(data).hexdigest()


def identity_sha(value):
    return byte_sha(json.dumps(value, sort_keys=True, separators=(',', ':')).encode())


def safe_child(root, relative):
    name = Path(relative)
    require(not name.is_absolute() and '..' not in name.parts, 'invalid artifact path: ' + relative)
    result = root / name
    require(result.resolve().is_relative_to(root.resolve()), 'artifact escapes root: ' + relative)
    return result


def file_map(root, hashes):
    return {str(p.relative_to(root)): hashes.file(p) for p in sorted(root.rglob('*')) if p.is_file()}


def verify_map(root, expected, hashes):
    for relative, value in expected.items():
        path = safe_child(root, relative)
        require(path.is_file() and hashes.file(path) == value, 'missing/corrupt artifact: ' + str(path))
    require(file_map(root, hashes) == expected, 'unrecorded or missing files: ' + str(root))


def index_rows(path):
    lines = path.read_text().splitlines()
    require(lines and lines[0].startswith('# pack\t'), 'pack index header missing')
    rows = {}
    for line in lines[1:]:
        if not line or line.startswith('#'):
            continue
        fields = line.split('\t')
        require(len(fields) == 4 and fields[0] not in rows and casc.norm_path(fields[0]) == fields[0],
                'invalid/duplicate/noncanonical index row')
        key = bytes.fromhex(fields[1])
        require(len(key) == 16 and int(fields[2]) > 0 and int(fields[3]) >= 0, 'invalid index key/size')
        rows[fields[0]] = line
    return lines[0], rows


def descriptor_map(cache):
    result = {entry['filename'].lower(): entry for entry in cache}
    require(len(result) == len(cache), 'duplicate descriptor name')
    return result


def metadata(entry):
    return {k: v for k, v in entry.items() if k not in ('format', 'thumbnailBase64')}


def is_normal(name):
    return Path(name).name.lower().endswith(('_nrm.texture', '_normal.texture', '_nrm$nrm.texture'))


def final_selection(before, after_map, accepted):
    protected = sorted(name for name in accepted if is_normal(name))
    composed = {name: path for name, path in accepted.items() if not is_normal(name)}
    final_cache = [after_map[e['filename'].lower()] if e['filename'].lower() in composed else e for e in before]
    return composed, protected, final_cache


def validate_batch(batch, raw, baseline_cache, baseline_index, hashes):
    """Caller holds the batch shared lock; no partial artifact is accepted."""
    report_path = batch / 'batch-report.json'
    require(report_path.is_file(), 'batch is partial: no final report')
    report = json.loads(report_path.read_text())
    require(report.get('complete') is True, 'batch is partial: complete flag absent')
    job = json.loads((batch / 'job.json').read_text())
    identity = {k: v for k, v in job.items() if k not in ('job_key', 'tool_inputs')}
    key = identity_sha(identity)
    require(key == job['job_key'] == report['job_key'], 'batch job identity mismatch')
    require(job['threads'] in (1, 2), 'batch exceeded approved encoder thread bound')
    tools = job['tool_inputs']['files']
    require(sorted((Path(p).name, h) for p, h in tools.items()) ==
            [tuple(pair) for pair in job['tool_hashes']], 'job tool identity mismatch')
    for path, value in tools.items():
        require(hashes.file(path) == value, 'batch tool input changed: ' + path)
    encoders = {command['argv'][0] for item in report['results'] for command in item.get('commands', [])}
    require(len(encoders) == 1 and all(hashes.file(p) == job['encoder_sha256'] for p in encoders),
            'encoder executable differs from job fingerprint')
    require(hashes.file(raw / 'data/build_manifest.psv') == job['source']['manifest_sha256'],
            'original manifest changed')
    manifest = casc.read_manifest(raw)
    original_cache = touch_layout._read_entry(str(raw), manifest[CACHE])
    require(byte_sha(original_cache) == job['source']['original_cache_sha256'], 'original cache changed')
    require(hashes.file(baseline_cache) == job['baseline']['full4_cache_sha256'] and
            hashes.file(baseline_index) == job['baseline']['full4_index_sha256'], 'full4 baseline changed')
    require(hashes.file(batch / 'repl.tsv') == report['repl_sha256'], 'batch replacement declaration changed')
    merged_path = batch / 'files' / CACHE
    require(hashes.file(merged_path) == report['merged_cache_sha256'], 'batch merged cache changed')
    verify_map(batch / 'files', {CACHE: report['merged_cache_sha256']}, hashes)
    before = json.loads(baseline_cache.read_text())
    after = json.loads(merged_path.read_text())
    before_map, after_map = descriptor_map(before), descriptor_map(after)
    require([e['filename'] for e in before] == [e['filename'] for e in after], 'descriptor list/order changed')
    selection = job['selection']
    require(selection and len(set(selection)) == len(selection) and
            all(casc.norm_path(name) == name and not name.endswith('_nrm.texture') for name in selection),
            'invalid/normal/duplicate batch selection')
    results = report['results']
    require([r['filename'] for r in results] == selection, 'incomplete or reordered batch results')
    checkpoint_files = {p.name for p in (batch / 'checkpoints').iterdir()}
    require(checkpoint_files == {byte_sha(n.encode()) + '.json' for n in selection},
            'incomplete or extra checkpoints')
    require({p.name for p in (batch / 'items').iterdir()} == {byte_sha(n.encode()) for n in selection},
            'incomplete or extra item directories')
    accepted, fallback = {}, []
    for item in results:
        name = item['filename']
        item_id = byte_sha(name.encode())
        checkpoint = json.loads((batch / 'checkpoints' / (item_id + '.json')).read_text())
        require(checkpoint == item and item['job_key'] == key, 'checkpoint/report disagreement: ' + name)
        directory = batch / 'items' / item_id
        verify_map(directory, item['files'], hashes)
        for command in item.get('commands', []):
            logfile = Path(command['log'])
            require(logfile.resolve().is_relative_to(directory.resolve()) and command['returncode'] == 0 and
                    hashes.file(logfile) == command['log_sha256'], 'encoder command/log mismatch')
        original = touch_layout._read_entry(str(raw), manifest[name])
        require(byte_sha(original) == item['input_sha256'], 'original texture changed: ' + name)
        status = item['status']
        require(status in ('converted', 'quality_fallback', 'unsupported_fallback'), 'unknown checkpoint status')
        if status != 'converted':
            require(item['baseline_preserved'] is True, 'fallback did not preserve baseline')
            fallback.append(name)
            continue
        require(not item['quality_failures'] and item['baseline_preserved'] is False, 'converted item failed quality')
        changed = json.loads((directory / 'descriptor.json').read_text())
        old = before_map[name]
        require(changed == after_map[name] and metadata(old) == metadata(changed), 'selected descriptor metadata changed')
        require((old['format'], changed['format']) in ((95, 99), (81, 85)), 'unexpected ASTC format conversion')
        texture = directory / 'texture'
        quality = json.loads((directory / 'quality.json').read_text())
        require(quality['input_sha256'] == item['input_sha256'] and
                quality['output_sha256'] == hashes.file(texture) and quality['ASTC_block_size'] == '6x6',
                'quality/output hash disagreement')
        require(all(m['retile_block_and_pixel_roundtrip'] and m['official_test_matches_actual_float_decode']
                    for m in quality['mips']), 'incomplete codec/layout oracle')
        accepted[name] = texture
    require(sorted(accepted) == report['changed_names'] and accepted, 'accepted-name declaration mismatch/empty')
    for name, entry in before_map.items():
        if name not in accepted:
            require(after_map[name] == entry, 'unselected/fallback/normal descriptor changed: ' + name)
    require(report['unchanged_baseline_entries'] == len(before) - len(accepted), 'unchanged descriptor count mismatch')
    declared = casc.read_replacements(batch / 'repl.tsv')
    expected = accepted | {CACHE: merged_path}
    require(set(declared) == set(expected) and all(declared[n].resolve() == p.resolve() for n, p in expected.items()),
            'batch replacement paths differ from verified checkpoints')
    composed, protected, final_cache = final_selection(before, after_map, accepted)
    require(composed, 'no non-normal images remain for composition')
    return {'job': job, 'report': report, 'accepted': composed, 'batch_accepted': accepted,
            'extra_normals_preserved': protected, 'final_cache': final_cache, 'fallback': fallback,
            'manifest': manifest, 'merged_cache': merged_path,
            'normal_count': sum(is_normal(n) for n in before_map)}


def cft_expected(before, manifest, updates):
    expected = bytearray(before)
    offset, size = struct.unpack_from('>II', before, 28)
    for name, (key, encoded_size, decoded_size) in updates.items():
        pos = casc.cft_position(before, offset, offset + size, manifest, name)
        expected[pos:pos + 16] = key
        casc.set_cft_sizes(expected, pos, encoded_size, decoded_size)
    return bytes(expected)


def root_content(source):
    _, values = casc.config_values(source / 'data/.build.config')
    key = bytes.fromhex(values['vfs-root'].split()[1])
    decoded, encoded = map(int, values['vfs-root-size'].split())
    data = casc.read_data_file(source / 'data', key, encoded)
    require(len(data) == decoded and data[:4] == b'TVFS', 'invalid root TVFS')
    return data, values


def prove_tvfs(before_source, after_source, updates):
    before, after = bytes(casc.load_vfs1(before_source)), bytes(casc.load_vfs1(after_source))
    manifest = casc.read_manifest(before_source)
    require(cft_expected(before, manifest, updates) == after, 'VFS changed outside selected CFT fields')
    root_before, old = root_content(before_source)
    root_after, new = root_content(after_source)
    old_key = bytes.fromhex(old['vfs-1'].split()[1])
    new_key = bytes.fromhex(new['vfs-1'].split()[1])
    hits = [i for i in range(len(root_before) - 15) if root_before[i:i + 16] == old_key]
    require(len(hits) == 1, 'root VFS reference is not unique')
    expected = bytearray(root_before)
    pos = hits[0]
    expected[pos:pos + 16] = new_key
    decoded, encoded = map(int, new['vfs-1-size'].split())
    casc.set_cft_sizes(expected, pos, encoded, decoded)
    require(bytes(expected) == root_after, 'root changed outside VFS reference fields')
    lines, _ = casc.config_values(before_source / 'data/.build.config')
    for key in ('vfs-1', 'vfs-1-size', 'vfs-root', 'vfs-root-size', 'vfs-2', 'vfs-2-size'):
        lines = casc.replace_config(lines, key, new[key])
    require(''.join(lines).encode() == (after_source / 'data/.build.config').read_bytes(),
            'build config changed outside VFS references')
    return {'VFS_only_selected_CFT_fields_changed': True, 'root_only_VFS_reference_changed': True,
            'config_only_VFS_keys_sizes_changed': True, 'VFS_decoded_bytes': len(after)}


def run_casc(arguments, out, logfile):
    require(sys.version_info >= (3, 13), 'Python3.13+ required for bounded original CASC process pool')
    command = [sys.executable, '-X', 'cpu_count=2', str(Path(casc.__file__).resolve()), *map(str, arguments)]
    with (out / logfile).open('w') as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)
    return command


def link_files(source, destination):
    for path in sorted(source.rglob('*')):
        target = destination / path.relative_to(source)
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            os.link(path.resolve(), target)


def compose(options):
    batch, raw, out = options.batch.resolve(), options.raw.resolve(), options.out.resolve()
    require(not out.exists(), 'output must be new; never reuse a partial composition')
    require((batch / '.lock').is_file(), 'batch lock absent')
    hashes = Hashes()
    with (batch / '.lock').open('r') as lock:
        fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
        verified = validate_batch(batch, raw, options.baseline_cache, options.baseline_index, hashes)
        overlay_record = json.loads(options.baseline_overlay_record.read_text())
        overlay_hashes = overlay_record['expected_hashes']
        verify_map(options.baseline_overlay, overlay_hashes, hashes)
        pack = json.loads(options.baseline_pack_manifest.read_text())
        require(pack['name'] == '00-00000008.data', 'expected full4 archive8 baseline')
        archive8 = options.baseline_pack_manifest.parent / pack['name']
        require(archive8.stat().st_size == pack['size'] and hashes.file(archive8) == pack['sha256'],
                'full4 archive size/hash mismatch')
        require(hashes.file(options.baseline_overlay / 'data' / pack['name']) == pack['sha256'] and
                hashes.file(options.baseline_overlay / 'pack.json') == hashes.file(options.baseline_pack_manifest),
                'overlay and full4 archive metadata differ')
        header, original_rows = index_rows(options.baseline_index)
        original = casc.read_prebuilt(options.baseline_index)
        for name, (key, encoded, _) in original.items():
            require(casc.data_filename(key[9], key[10]) == pack['name'] and
                    int.from_bytes(key[11:], 'big') + encoded <= pack['size'], 'full4 index points outside verified archive')
        cache_key, cache_encoded, cache_decoded = original[CACHE]
        row = list(verified['manifest'][CACHE])
        row[2:8] = [str(cache_key[9]), str(cache_key[10]),
                    hashlib.md5(options.baseline_cache.read_bytes()).hexdigest(),
                    str(cache_decoded), cache_key.hex(), str(cache_encoded)]
        baseline_content = touch_layout._read_entry(str(options.baseline_overlay), row)
        require(byte_sha(baseline_content) == verified['job']['baseline']['full4_cache_sha256'],
                'full4 archive descriptor cache differs from supplied baseline cache')
        require(set(verified['accepted']) | {CACHE} <= set(original), 'new entries absent from full4 index')
        for name in (PACK9, ROOT10):
            require(not (raw / 'data' / name).exists() and not (options.baseline_overlay / 'data' / name).exists(),
                    'composition archive location already in use: ' + name)
        require(sys.version_info >= (3, 13), 'Python3.13+ required for bounded CASC workers')
        count = subprocess.check_output([sys.executable, '-X', 'cpu_count=2', '-c',
                                         'import os; print(os.process_cpu_count())'], text=True).strip()
        require(count == '2', 'CASC child CPU count restriction is unavailable')
        source_tree = Path(__file__).resolve().parents[2]
        revision = subprocess.check_output(['git', '-C', str(source_tree), 'rev-parse', 'HEAD'], text=True).strip()
        require(not subprocess.check_output(['git', '-C', str(source_tree), 'status', '--porcelain'], text=True),
                'composition source must be frozen and clean')
        # All completed-batch/base input verification precedes creating any output.
        out.mkdir(parents=True)
        final_cache_path = out / 'files' / CACHE
        final_cache_path.parent.mkdir(parents=True)
        final_cache_path.write_text(json.dumps(verified['final_cache'], ensure_ascii=False, separators=(',', ':')))
        replacements = verified['accepted'] | {CACHE: final_cache_path}
        declaration = out / 'repl.tsv'
        declaration.write_text(''.join(f'{n}\t{p}\n' for n, p in sorted(replacements.items())))
        commands = [run_casc(['pack', raw, out / 'pack9', declaration, '0-9'], out, 'pack.log')]
        _, update_rows = index_rows(out / 'pack9/pack_index.tsv')
        updates = casc.read_prebuilt(out / 'pack9/pack_index.tsv')
        require(set(updates) == set(replacements), 'archive9 index has unexpected entries')
        combined = original_rows | update_rows
        require(all(combined[n] == line for n, line in original_rows.items() if n not in updates),
                'fallback/normal/unselected index rows changed')
        (out / 'merged-pack-index.tsv').write_text(header + '\n' + '\n'.join(combined.values()) + '\n')
        source = out / 'composed-source'
        (source / 'data').mkdir(parents=True)
        for path in raw.iterdir():
            if path.name != 'data':
                (source / path.name).symlink_to(path.resolve(), target_is_directory=path.is_dir())
        for path in (raw / 'data').iterdir():
            if path.name not in ('.build.config', 'build_manifest.psv'):
                (source / 'data' / path.name).symlink_to(path.resolve(), target_is_directory=path.is_dir())
        for path in (options.baseline_overlay / 'data').iterdir():
            target = source / 'data' / path.name
            if target.exists() or target.is_symlink():
                target.unlink()
            target.symlink_to(path.resolve(), target_is_directory=path.is_dir())
        (source / 'data' / PACK9).symlink_to((out / 'pack9' / PACK9).resolve())
        rows, rewritten = [], []
        for line in (raw / 'data/build_manifest.psv').read_text().splitlines():
            fields = line.split('|')
            name = casc.norm_path(fields[9]) if len(fields) >= 11 else ''
            if name in updates:
                fields[6] = original[name][0].hex()
                rewritten.append(name)
            rows.append('|'.join(fields))
        require(len(rewritten) == len(updates) and set(rewritten) == set(updates), 'virtual manifest lookup mismatch')
        (source / 'data/build_manifest.psv').write_text('\n'.join(rows) + '\n')
        for name, (key, encoded, decoded) in updates.items():
            row = list(verified['manifest'][name])
            row[2:8] = [str(key[9]), str(key[10]), hashlib.md5(replacements[name].read_bytes()).hexdigest(),
                        str(decoded), key.hex(), str(encoded)]
            roundtrip = touch_layout._read_entry(str(source), row)
            require(byte_sha(roundtrip) == hashes.file(replacements[name]), 'archive9 content roundtrip failed: ' + name)
        require(casc.available_location(source / 'data') == (0, 10), 'root TVFS would not use archive0a')
        empty = out / 'empty.tsv'
        empty.write_text('')
        commands.append(run_casc([source, out / 'casc-overlay', empty, '--prebuilt', out / 'pack9/pack_index.tsv'],
                                 out, 'overlay.log'))
        proof = prove_tvfs(source, out / 'casc-overlay', updates)
        deploy = out / 'romfs_overlay_astc'
        deploy.mkdir()
        link_files(options.baseline_overlay, deploy)
        for name, path in {'.build.config': out / 'casc-overlay/data/.build.config',
                           ROOT10: out / 'casc-overlay/data' / ROOT10,
                           PACK9: out / 'pack9' / PACK9}.items():
            target = deploy / 'data' / name
            target.unlink(missing_ok=True)
            os.link(path, target)
        for name, value in overlay_hashes.items():
            if name != 'data/.build.config':
                require(hashes.file(deploy / name) == value, 'inherited overlay changed: ' + name)
        # Reverify recorded inputs after production; the shared batch lock is still held.
        verify_map(options.baseline_overlay, overlay_hashes, hashes)
        python_paths = [Path(sys.executable).resolve()]
        if getattr(casc.zlib, '__file__', None):
            python_paths.append(Path(casc.zlib.__file__))
        dependencies = subprocess.run(['ldd', sys.executable], capture_output=True, text=True, check=True)
        for token in dependencies.stdout.replace('=>', ' ').split():
            if token.startswith('/') and Path(token).is_file():
                python_paths.append(Path(token).resolve())
        inputs = {str(p.resolve()): hashes.file(p) for p in [batch / 'job.json', batch / 'batch-report.json',
                  batch / 'repl.tsv', options.baseline_cache, options.baseline_index, options.baseline_pack_manifest,
                  options.baseline_overlay_record, raw / 'data/build_manifest.psv',
                  raw / 'data/.build.config',
                  Path(__file__), Path(casc.__file__), Path(touch_layout.__file__), *python_paths]}
        report = {'complete': True, 'not_deployed': True, 'batch_job_key': verified['job']['job_key'],
                  'source_revision': revision, 'source_clean': True,
                  'python_version': sys.version, 'zlib_runtime_version': casc.zlib.ZLIB_RUNTIME_VERSION,
                  'python_ldd_output': dependencies.stdout,
                  'zlib_module': getattr(casc.zlib, '__file__', 'built-in; fingerprinted in Python runtime libraries'),
                  'inputs': inputs, 'commands': commands, 'CASC_workers': 2,
                  'selected': sorted(verified['accepted']), 'quality_or_unsupported_fallback': verified['fallback'],
                  'batch_converted': sorted(verified['batch_accepted']),
                  'extra_normal_aliases_preserved': verified['extra_normals_preserved'],
                  'final_cache_sha256': hashes.file(final_cache_path),
                  'normal_entries_preserved': verified['normal_count'], 'unselected_descriptors_and_index_exact': True,
                  'UI_and_full4_archive_exact': True, 'TVFS_proof': proof,
                  'deploy_overlay': str(deploy), 'deploy_files': file_map(deploy, hashes),
                  'new_archives': {PACK9: hashes.file(deploy / 'data' / PACK9),
                                   ROOT10: hashes.file(deploy / 'data' / ROOT10)},
                  'virtual_manifest_sha256': hashes.file(source / 'data/build_manifest.psv'),
                  'raw_VFS_decoded_sha256': byte_sha(casc.load_vfs1(raw)),
                  'baseline_VFS_decoded_sha256': byte_sha(casc.load_vfs1(source)),
                  'pack9_index_sha256': hashes.file(out / 'pack9/pack_index.tsv'),
                  'merged_index_sha256': hashes.file(out / 'merged-pack-index.tsv'),
                  'limits': 'No APK rebuild, deployment or GPU/FPS/thermal claim. Requires original raw RomFS and inherited full4/UI artifacts.'}
        report['artifacts'] = {str(p.relative_to(out)): hashes.file(p) for p in sorted(out.rglob('*'))
                               if p.is_file() and not p.is_symlink()}
        temporary = out / 'composition.json.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(out / 'composition.json')
        print(out / 'composition.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('batch', 'raw', 'out', 'baseline-cache', 'baseline-index', 'baseline-pack-manifest',
                 'baseline-overlay', 'baseline-overlay-record'):
        parser.add_argument('--' + name, required=True, type=Path)
    compose(parser.parse_args())


if __name__ == '__main__':
    main()
