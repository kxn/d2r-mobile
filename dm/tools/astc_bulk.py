#!/usr/bin/env python3
"""Explicit offline ASTC6 batches over an immutable full4 baseline; no deployment.

The normal astc_pack.py default remains 4x4. This opt-in tool reuses
astc_repack.convert(), original NVN C layout and the actual astcenc executable.
"""
import argparse
from collections import Counter
import fcntl
import json
import math
from pathlib import Path
import re
import shutil
import sys
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ios/tools'))

import astc_repack as repack
import casc_overlay
import touch_layout

CACHE = 'data/hd/texture_desc_cache.json'


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    temporary.replace(path)


def digest(value):
    return repack.sha(json.dumps(value, sort_keys=True, separators=(',', ':')).encode())


def load_source(root):
    manifest_path = root / 'data/build_manifest.psv'
    manifest = manifest_path.read_bytes()
    rows = {}
    for line in manifest.decode('utf-8').splitlines():
        row = line.split('|')
        if len(row) >= 11 and not row[0].startswith('#'):
            name = casc_overlay.norm_path(row[9])
            rows.setdefault(name, row)
    raw = touch_layout._read_entry(str(root), rows[CACHE])
    cache = json.loads(raw)
    entries = {e['filename'].lower(): e for e in cache}
    if len(entries) != len(cache):
        raise ValueError('duplicate case-insensitive descriptor name')
    return rows, entries, {'manifest_sha256': repack.sha(manifest),
                           'original_cache_sha256': repack.sha(raw)}


def category(name, entry):
    # Same suffix convention as upstream, always preserve NRM baseline bytes.
    if name.endswith('_nrm.texture'):
        return 'normal_keep4'
    if name.endswith('_orm.texture'):
        return 'orm'
    if name.endswith('_alb.texture'):
        return 'albedo'
    return 'other_srgb' if entry.get('format') == 62 else 'other_linear'


def inventory(rows, entries, fingerprints):
    items, groups = [], {}
    for name, entry in sorted(entries.items()):
        if entry.get('format') not in repack.GAME_FORMAT:
            continue
        kind = category(name, entry)
        item = {'filename': name, 'category': kind, 'format': entry.get('format'),
                'dimensions': [entry.get('width'), entry.get('height')],
                'levels': entry.get('levels'), 'candidate': False}
        row = rows.get(name)
        if row:
            item.update({'decoded_source_bytes': int(row[5]), 'encoded_source_bytes': int(row[7])})
        try:
            if row is None:
                raise repack.Unsupported('no original manifest row')
            if kind == 'normal_keep4':
                raise repack.Unsupported('normal suffix preserved as exact full4 baseline')
            old = repack.desc(entry, repack.NVN_BC3[entry['format']])
            new = repack.desc(entry, repack.NVN_ASTC6[entry['format']])
            item.update({'candidate': True, 'C_NVN_storage_bytes':
                         [repack.geometry(old)[-1], repack.geometry(new)[-1]],
                         'linear_block_bytes': [sum(repack.block_bytes(*repack.dimensions(old.width, old.height, i), b)
                                                    for i in range(old.levels)) for b in (4, 6)]})
        except repack.Unsupported as error:
            item['keep4_reason'] = str(error)
        group = groups.setdefault(kind, {'count': 0, 'candidates': 0, 'decoded_source_bytes': 0,
                                         'encoded_source_bytes': 0, 'dimensions': Counter(), 'levels': Counter()})
        group['count'] += 1
        group['candidates'] += int(item['candidate'])
        for key in ('decoded_source_bytes', 'encoded_source_bytes'):
            group[key] += item.get(key, 0)
        group['dimensions']['x'.join(map(str, item['dimensions']))] += 1
        group['levels'][str(item['levels'])] += 1
        items.append(item)
    return {'schema': 1, 'source': fingerprints, 'groups': groups, 'items': items,
            'scope': 'descriptor/manifest preflight only; full header, thumbnail and quality checked during conversion',
            'normal_policy': 'preserve exact existing full4 files/cache',
            'VkImage_memory_requirements_and_GPU_bandwidth': 'not measured'}


def quality_failures(report, policy):
    failures = []
    # Thresholds are a conservative selection recipe, not a visual equivalence proof.
    for mip in report['mips']:
        w, h = mip['dimensions']
        if min(w, h) >= 8:
            metric = mip['official_astcenc_metrics'].get('PSNR (LDR-RGB)', '')
            try:
                value = float(metric.split()[0])
            except (ValueError, IndexError):
                value = float('nan')
            floor = policy['min_rgb_psnr']
            if math.isnan(value) or value < floor:
                failures.append(f"mip{mip['level']}: RGB PSNR {metric!r} below {floor}")
        channels = mip['channels']
        alpha_range = mip.get('source_alpha_range')
        if (alpha_range and alpha_range[0] == alpha_range[1] and
                channels['RGBA_max_abs'][3] > policy.get('constant_alpha_max_error', 0)):
            failures.append(f"mip{mip['level']}: constant alpha {alpha_range[0]} changed")
        if channels['RGBA_max_abs'][3] > policy['max_alpha_error']:
            failures.append(f"mip{mip['level']}: alpha error exceeds {policy['max_alpha_error']}")
        for threshold, counts in channels['alpha_coverage'].items():
            if threshold == '255':
                continue  # 254->255 is not a transparent/cutout coverage change.
            if abs(counts['after'] - counts['before']) > w * h * policy['max_alpha_coverage_fraction']:
                failures.append(f"mip{mip['level']}: alpha coverage {threshold} changed beyond recipe")
    return failures


def checked_files(directory):
    return {str(p.relative_to(directory)): repack.sha(p.read_bytes())
            for p in sorted(directory.rglob('*')) if p.is_file()}


def verify_files(directory, files):
    for name, expected in files.items():
        path = directory / name
        if not path.is_file() or repack.sha(path.read_bytes()) != expected:
            raise ValueError('checkpoint artifact missing or corrupt: ' + str(path))


def encoder_summary(commands):
    # Use the existing encoder's performance output; no custom timing/sampling.
    coding_seconds, pixels, count = 0.0, 0.0, 0
    for command in commands:
        if Path(command['log']).name != 'quality.log':
            continue
        text = Path(command['log']).read_text()
        timing = re.search(r'Coding time:\s+([0-9.]+) s', text)
        rate = re.search(r'Coding rate:\s+([0-9.]+) MT/s', text)
        if timing and rate:
            seconds = float(timing[1])
            coding_seconds += seconds
            pixels += seconds * float(rate[1]) * 1e6
            count += 1
    return {'official_quality_encode_calls': count, 'official_coding_seconds': coding_seconds,
            'official_coding_pixels_from_reported_rate': pixels,
            'aggregate_official_MT_per_second': pixels / coding_seconds / 1e6 if coding_seconds else None,
            'boundary': 'astcenc quality re-encode only; excludes first encode, decode, I/O, layout and full batch elapsed'}


def load_baseline(cache_path, index_path):
    raw, index_raw = cache_path.read_bytes(), index_path.read_bytes()
    cache = json.loads(raw)
    entries = {e['filename'].lower(): i for i, e in enumerate(cache)}
    if len(entries) != len(cache):
        raise ValueError('duplicate baseline descriptor names')
    prebuilt = casc_overlay.read_prebuilt(index_path)
    return cache, entries, prebuilt, {'full4_cache_sha256': repack.sha(raw),
                                     'full4_index_sha256': repack.sha(index_raw)}


def batch(options):
    if options.limit < 1:
        raise ValueError('explicit positive --limit required; no implicit full conversion')
    root, out = options.romfs.resolve(), options.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    with (out / '.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        rows, entries, source = load_source(root)
        baseline, baseline_indices, prebuilt, baseline_inputs = load_baseline(options.baseline_cache, options.baseline_index)
        if set(entries) != set(baseline_indices):
            raise ValueError('baseline must contain the complete original descriptor name set')
        preserved_baseline = list(baseline)
        selection = [casc_overlay.norm_path(line) for line in options.selection.read_text().splitlines()
                     if line.strip() and not line.lstrip().startswith('#')]
        if not selection or len(set(selection)) != len(selection) or len(selection) > options.limit:
            raise ValueError('selection must be unique, nonempty and bounded by --limit')
        for name in selection:
            if name not in entries or name not in rows or name not in baseline_indices:
                raise ValueError('selection absent from source/baseline: ' + name)
            if name.endswith('_nrm.texture'):
                raise ValueError('normal conversion forbidden in bulk recipe: ' + name)
            if not name.startswith('data/') or '..' in Path(name).parts:
                raise ValueError('invalid logical path: ' + name)
        encoder = repack.Encoder(options.astcenc, options.preset, options.threads)
        tools = repack.tool_inputs(encoder)
        tools['files'][str(Path(__file__).resolve())] = repack.sha(Path(__file__).read_bytes())
        tools['files'][str(Path(casc_overlay.__file__).resolve())] = repack.sha(Path(casc_overlay.__file__).read_bytes())
        policy = {'min_rgb_psnr': options.min_rgb_psnr, 'max_alpha_error': options.max_alpha_error,
                  'max_alpha_coverage_fraction': options.max_alpha_coverage_fraction,
                  'constant_alpha_max_error': options.constant_alpha_max_error}
        if not (0 <= policy['min_rgb_psnr'] <= 100 and 0 <= policy['max_alpha_error'] <= 255 and
                0 <= policy['constant_alpha_max_error'] <= 255 and
                0 <= policy['max_alpha_coverage_fraction'] <= 1):
            raise ValueError('invalid quality policy')
        identity = {'source': source, 'baseline': baseline_inputs, 'selection': selection,
                    'preset': encoder.preset, 'threads': encoder.threads, 'encoder_version': encoder.version,
                    'encoder_sha256': repack.sha(Path(encoder.path).read_bytes()), 'quality_policy': policy,
                    'tool_hashes': sorted((Path(p).name, h) for p, h in tools['files'].items())}
        job_key = digest(identity)
        job_path = out / 'job.json'
        if job_path.exists() and json.loads(job_path.read_text())['job_key'] != job_key:
            raise ValueError('resume inputs/recipe changed; use a new output directory')
        atomic_json(job_path, {'job_key': job_key, **identity, 'tool_inputs': tools})
        # Prevent a failed resumed invocation from leaving a stale usable declaration.
        for filename in ('repl.tsv', 'batch-report.json'):
            (out / filename).unlink(missing_ok=True)
        results, reused = [], 0
        for name in selection:
            item_id = repack.sha(name.encode())
            checkpoint = out / 'checkpoints' / (item_id + '.json')
            directory = out / 'items' / item_id
            data = touch_layout._read_entry(str(root), rows[name])
            input_sha = repack.sha(data)
            if checkpoint.exists():
                item = json.loads(checkpoint.read_text())
                if item['job_key'] != job_key or item['input_sha256'] != input_sha:
                    raise ValueError('checkpoint input mismatch: ' + name)
                verify_files(directory, item['files'])
                reused += 1
            else:
                if directory.exists():
                    shutil.rmtree(directory)  # Only this job's uncommitted owned output.
                directory.mkdir(parents=True)
                start_commands = len(encoder.commands)
                item = {'filename': name, 'job_key': job_key, 'input_sha256': input_sha,
                        'baseline_preserved': True}
                try:
                    entry = entries[name]
                    repack.desc(entry, repack.NVN_BC3.get(entry.get('format'), 0))
                    baseline_entry = baseline[baseline_indices[name]]
                    if name not in prebuilt or baseline_entry['format'] != repack.GAME_FORMAT4[entry['format']]:
                        raise repack.Unsupported('no matching prebuilt full4 texture to preserve on fallback')
                    metadata = lambda e: {k: v for k, v in e.items() if k not in ('format', 'thumbnailBase64')}
                    if metadata(entry) != metadata(baseline_entry):
                        raise ValueError('original/full4 descriptor metadata differ: ' + name)
                    output, changed, report = repack.convert(data, entry, encoder, directory / 'quality', 6)
                    for mip in report['mips']:
                        with Image.open(directory / 'quality' / f"mip-{mip['level']}" / 'source.png') as image:
                            mip['source_alpha_range'] = list(image.getchannel('A').getextrema())
                    report['tiny_mip_linear_bytes'] = sum(m['linear_bytes'] for m in report['mips']
                                                         if min(m['dimensions']) < 8)
                    rejected = quality_failures(report, policy)
                    (directory / 'texture').write_bytes(output)
                    atomic_json(directory / 'descriptor.json', changed)
                    atomic_json(directory / 'quality.json', report)
                    item.update({'status': 'quality_fallback' if rejected else 'converted',
                                 'quality_failures': rejected, 'baseline_preserved': bool(rejected),
                                 'file_bytes': report['file_bytes'],
                                 'C_NVN_storage_bytes': report['C_NVN_storage_bytes'],
                                 'linear_sampled_block_bytes': report['linear_sampled_block_bytes']})
                except repack.Unsupported as error:
                    item.update({'status': 'unsupported_fallback', 'reason': str(error)})
                item['commands'] = encoder.commands[start_commands:]
                item['files'] = checked_files(directory)
                atomic_json(checkpoint, item)  # Publish success only after all artifacts exist.
            results.append(item)
            print(f"{name}: {item['status']}", flush=True)
        replacements, changed_names = [], set()
        for item in results:
            if item['status'] != 'converted':
                continue
            name = item['filename']
            directory = out / 'items' / repack.sha(name.encode())
            baseline[baseline_indices[name]] = json.loads((directory / 'descriptor.json').read_text())
            replacements.append((name, directory / 'texture'))
            changed_names.add(name)
        for name, index in baseline_indices.items():
            if name not in changed_names and baseline[index] != preserved_baseline[index]:
                raise ValueError('unselected baseline descriptor changed: ' + name)
        # One full-cache merge/write for the entire batch, never once per texture.
        destination = out / 'files' / CACHE
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(baseline, ensure_ascii=False, separators=(',', ':')))
        replacements.append((CACHE, destination))
        declaration = out / 'repl.tsv'
        declaration.write_text(''.join(f'{name}\t{path}\n' for name, path in replacements))
        commands = [command for item in results for command in item.get('commands', [])]
        atomic_json(out / 'batch-report.json', {'complete': True, 'job_key': job_key,
                    'not_deployed': True, 'GPU_bandwidth_not_measured': True,
                    'results': results, 'checkpoints_reused': reused,
                    'unchanged_baseline_entries': len(baseline) - len(changed_names),
                    'changed_names': sorted(changed_names), 'official_encoder_work': encoder_summary(commands),
                    'merged_cache_sha256': repack.sha(destination.read_bytes()),
                    'repl_sha256': repack.sha(declaration.read_bytes()),
                    'composition': 'overlay these replacements on the original full4 pack; keep every other prebuilt entry unchanged'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    inv = commands.add_parser('inventory')
    inv.add_argument('romfs', type=Path)
    inv.add_argument('out', type=Path)
    run = commands.add_parser('batch')
    run.add_argument('romfs', type=Path)
    run.add_argument('out', type=Path)
    run.add_argument('--selection', required=True, type=Path)
    run.add_argument('--limit', required=True, type=int)
    run.add_argument('--baseline-cache', required=True, type=Path)
    run.add_argument('--baseline-index', required=True, type=Path)
    run.add_argument('--astcenc', required=True)
    run.add_argument('--preset', choices=['fastest', 'fast', 'medium', 'thorough', 'verythorough', 'exhaustive'], default='thorough')
    run.add_argument('--threads', choices=[1, 2], type=int, default=2)
    run.add_argument('--min-rgb-psnr', type=float, default=32.0)
    run.add_argument('--max-alpha-error', type=int, default=8)
    run.add_argument('--constant-alpha-max-error', type=int, default=0)
    run.add_argument('--max-alpha-coverage-fraction', type=float, default=0.001)
    options = parser.parse_args()
    if options.command == 'inventory':
        rows, entries, source = load_source(options.romfs)
        atomic_json(options.out, inventory(rows, entries, source))
    else:
        batch(options)


if __name__ == '__main__':
    main()
