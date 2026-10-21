"""Explicit cold-source selection; no native layout or runtime authority inferred."""
import csv
import hashlib
import json
import math
from pathlib import Path

KIND = 'explicit-hash-pinned-nonUI-single-layer-ASTC4-v1'
LEGACY_KIND = 'act1env-and-characters-excluding-declared-terrain-array'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def record(path):
    path = Path(path).resolve()
    return {'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size}


def pinned(item):
    path = Path(item['path'])
    require(path.is_absolute() and sha(path) == item['sha256'], 'selection provenance hash differs')
    if 'bytes' in item:
        require(path.stat().st_size == item['bytes'], 'selection provenance size differs')
    return path


def logical_name(name):
    require(isinstance(name, str) and name.startswith('data/hd/') and name.endswith('.texture'),
            'not a source HD texture logical')
    require(name == name.lower() and '\\' not in name and all(part not in ('', '.', '..') for part in name.split('/')),
            'logical must already be normalized')
    require('ui' not in name.split('/'), 'UI is excluded from explicit native selection')
    return name


def eligible(entry):
    # Matches original single-resource desc() before original CFT/mip proof.
    dims = [entry.get(k) for k in ('width', 'height', 'levels', 'layoutData')]
    return (all(type(v) is int for v in dims) and entry.get('format') in (81, 95)
            and entry.get('type') == 2 and entry.get('depth') == 1 and entry.get('layoutType') == 3
            and 0 < dims[0] <= 4096 and 0 < dims[1] <= 4096
            and 0 < dims[2] <= min(16, max(dims[:2]).bit_length())
            and 0 <= dims[3] < 1 << 64 and dims[3] & 255 <= 5
            and (not entry.get('thumbnailBase64') or 0 < entry.get('thumbnailMip', 0) < dims[2]))


def explicit(path, expected_sha, context, arrays):
    require(sha(path) == expected_sha, 'explicit logical manifest hash differs')
    value = json.loads(Path(path).read_text())
    require(value['schema'] == 1 and value['kind'] == KIND, 'unsupported explicit selection schema')
    names = value['selected_logicals']
    require(isinstance(names, list) and names and names == sorted(set(names)), 'selection must be nonempty sorted unique logicals')
    source = json.loads(pinned(value['source_receipt']).read_text())
    require(source['schema'] == 1 and source['source_cache'] == context.cache_info
            and source['source_index'] == context.common['source_index'], 'selection source catalog identity differs')
    catalog = {row['logical']: row['descriptor'] for row in source['entries']}
    require(len(catalog) == len(source['entries']), 'source receipt has duplicate logicals')
    reuse_path = pinned(value['reuse_source_pack'])
    reuse = json.loads(reuse_path.read_text())
    require(reuse['schema'] == 1 and reuse['complete'] is True and reuse['selection']['kind'] == LEGACY_KIND,
            'reuse source manifest must be complete historical selection')
    require(reuse['source_context']['source_cache'] == context.cache_info and
            reuse['source_context']['source_index'] == context.common['source_index'], 'reuse source provenance differs')
    require(source['array_closure'] == reuse['selection']['array_closure'], 'array closure differs from historical source')
    previous = {row['logical'] for row in reuse['entries']}
    require(len(previous) == len(reuse['entries']), 'duplicate reuse source logicals')
    for name in names:
        logical_name(name)
        require(name not in arrays, 'declared array member is excluded')
        require(name not in previous, 'already-selected native entry must reuse its original actual bake')
        require(name in context.entries and name in catalog and context.entries[name] == catalog[name],
                'logical or descriptor differs from pinned source catalog')
        require(eligible(context.entries[name]), 'unsupported single-layer ASTC4 descriptor')
    return names, {'kind': KIND, 'formats': [81, 95], 'logical_manifest': record(path),
                   'source_receipt': value['source_receipt'], 'reuse_source_pack': value['reuse_source_pack'],
                   'static_file_origin_is_not_runtime_immutability': True}


def quality_gate(provenance, names, root):
    # Use original encoder's positive quality result; never re-encode or score.
    item = next(row for row in provenance['source_and_artifact_verification'] if Path(row['path']).name == 'quality.tsv')
    path = Path(item['path'])
    if not path.is_absolute():
        path = root / path
    require(item['matches'] is True and sha(path) == item['sha256'], 'original ASTC quality fingerprint differs')
    with path.open() as stream:
        rows = csv.reader(stream, delimiter='\t')
        next(rows)
        quality = {}
        for row in rows:
            require(len(row) == 6 and row[0] not in quality, 'invalid original quality table')
            quality[row[0]] = row
    for name in names:
        require(name in quality and math.isfinite(float(quality[name][5])) and float(quality[name][5]) > 0,
                'selection lacks original positive ASTC quality result')
    return record(path)
