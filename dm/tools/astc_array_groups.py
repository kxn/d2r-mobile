"""Original declared texture-array membership and monotone ASTC selection closure.

Offline only. Membership comes from typed Biome/TerrainData declarations, never
from texture filenames, scene names, runtime guesses or encoder quality scores.
"""
import hashlib
import json
import re

ROLES = ('Albedo', 'Normal', 'ORM', 'Noise', 'GrassTexture')
TIERS = ('terrainDataUltra', 'terrainDataHigh', 'terrainDataMed', 'terrainDataLow')
# Unparsed JSON must not conceal an array declaration. Unrelated original UI
# JSON-with-comments is recorded, not rewritten or interpreted as membership.
DECLARATION = re.compile(rb'terrainlayers|terraindata|texture_?array', re.I)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def texture_name(value):
    if not isinstance(value, str) or not value.lower().endswith('.texture'):
        return None
    name = value.replace('\\', '/').lower()
    require(name.startswith('data/') and '..' not in name.split('/') and ':' not in name,
            'noncanonical declared texture member: ' + value)
    return name


def walk(value, pointer=''):
    if isinstance(value, dict):
        yield pointer, value
        for key, child in value.items():
            yield from walk(child, pointer + '/' + key.replace('~', '~0').replace('/', '~1'))
    elif isinstance(value, list):
        for i, child in enumerate(value):
            yield from walk(child, pointer + '/' + str(i))


def biome_groups(name, document):
    require(document.get('type') == 'Biome', 'unsupported biome declaration: ' + name)
    groups = []
    declared = set()
    for tier in TIERS:
        data = document.get(tier)
        require(isinstance(data, dict) and data.get('type') == 'TerrainData' and
                isinstance(data.get('terrainLayers'), list), 'unknown TerrainData schema: ' + name + '/' + tier)
        declared.add('/' + tier)
        layers = data['terrainLayers']
        roles = set(ROLES)
        for layer in layers:
            require(isinstance(layer, dict), 'unknown TerrainLayer schema: ' + name + '/' + tier)
            # An added explicit texture-valued role is preserved, not silently
            # ignored. Its unknown assembly semantics cannot authorize ASTC6.
            roles.update(k for k, v in layer.items() if texture_name(v))
        for role in sorted(roles):
            members, unknown = [], []
            if not layers:
                unknown.append('empty-terrain-layers')
            if role not in ROLES:
                unknown.append('unsupported-texture-role')
            for i, layer in enumerate(layers):
                member = texture_name(layer.get(role))
                if member is None or layer.get('type') != 'TerrainLayer':
                    unknown.append('missing-or-unknown-layer:' + str(i))
                if member is not None:
                    members.append(member)
            groups.append({'id': name + '/' + tier + '/' + role, 'source': name,
                           'pointer': '/' + tier + '/terrainLayers', 'role': role,
                           'ordered_members': members, 'members': sorted(set(members)),
                           'unknown': unknown})
    # Newly nested TerrainData requires an explicit reviewed parser; do not miss
    # a future tier or turn an arbitrary texture list into an array by guesswork.
    require(all(pointer in declared for pointer, node in walk(document)
                if node.get('type') == 'TerrainData'), 'unrecognized TerrainData location: ' + name)
    return groups


def discover(manifest, read_entry):
    """Read MD5-verified original entries through the existing CASC reader.

    All original JSON content hashes form the discovery census. Only the exact
    typed schema above authorizes membership. Unknown explicit TextureArray
    schemas fail closed, rather than claiming a partial member list is complete.
    """
    sources, unparsed, groups, group_sources = [], [], [], []
    for name, row in sorted(manifest.items()):
        if not name.endswith('.json'):
            continue
        content = read_entry(row)
        record = {'path': name, 'sha256': hashlib.sha256(content).hexdigest(),
                  'bytes': len(content), 'manifest_md5': row[4]}
        sources.append(record)
        try:
            document = json.loads(content)
        except (ValueError, UnicodeError) as error:
            require(not DECLARATION.search(content), 'unparsed possible array declaration: ' + name)
            unparsed.append({'path': name, 'reason': str(error)})
            continue
        nodes = list(walk(document))
        for pointer, node in nodes:
            kind = node.get('type', '')
            normalized = kind.lower().replace('_', '') if isinstance(kind, str) else ''
            require('texturearray' not in normalized and
                    all('texturearray' not in key.lower().replace('_', '') for key in node),
                    'unsupported explicit TextureArray declaration: ' + name + pointer)
        if isinstance(document, dict) and document.get('type') == 'Biome':
            groups.extend(biome_groups(name, document))
            group_sources.append(record)
        else:
            require(not any(node.get('type') in ('Biome', 'TerrainData') for _, node in nodes),
                    'unsupported nested biome/terrain declaration: ' + name)
    return groups, {'schema': 'original-declared-array-format-closure/v1',
                    'json_census': sources, 'unparsed_unrelated_json': unparsed,
                    'group_sources': group_sources,
                    'scope': 'Typed original Biome four TerrainData tiers, each explicit texture role separately. '
                             'Unknown explicit TextureArray/terrain schemas reject composition. '
                             'No claim about runtime arrays without original declarations.'}


def close_selection(before_map, after_map, selected, groups, proof, preserved_reasons=None):
    """Remove only, until shared declared members reach a fixed point."""
    preserved_reasons = preserved_reasons or {}
    result = dict(selected)
    evidence = []
    for group in groups:
        item = dict(group)
        item['missing_descriptors'] = [n for n in group['members'] if n not in before_map]
        item['baseline_formats'] = sorted({before_map[n].get('format') for n in group['members']
                                           if n in before_map}, key=str)
        item['candidate_formats'] = sorted({after_map[n].get('format') for n in group['members']
                                            if n in after_map}, key=str)
        evidence.append(item)
    rounds, events = [], []
    while True:
        removed = set()
        for group in evidence:
            members = set(group['members'])
            chosen = members & result.keys()
            if not chosen:
                continue
            absent = sorted(members - result.keys())
            reasons = []
            if group['unknown']:
                reasons.append('incomplete-or-unsupported-member-declaration')
            if group['missing_descriptors'] or None in group['baseline_formats']:
                reasons.append('missing-descriptor-or-format')
            if len(group['baseline_formats']) != 1:
                reasons.append('baseline-format-not-homogeneous')
            if absent:
                reasons.append('member-not-selected')
            if not absent and (len(group['candidate_formats']) != 1 or None in group['candidate_formats']):
                reasons.append('candidate-format-not-homogeneous')
            if reasons:
                removed.update(chosen)
                events.append({'round': len(rounds) + 1, 'group': group['id'], 'reasons': reasons,
                               'absent_members': {n: preserved_reasons.get(n, 'not-selected-or-propagated')
                                                  for n in absent}, 'preserved_members': sorted(chosen)})
        if not removed:
            break
        rounds.append(sorted(removed))
        for name in removed:
            del result[name]
    # Every changed declared array must now be complete and one format. Arrays
    # left baseline remain byte-for-byte baseline, including preexisting quirks.
    for group in evidence:
        if set(group['members']) & result.keys():
            require(not group['unknown'] and not group['missing_descriptors'] and
                    set(group['members']) <= result.keys() and
                    len(group['baseline_formats']) == len(group['candidate_formats']) == 1,
                    'array selection closure failed: ' + group['id'])
    proof = dict(proof, groups=evidence, rounds=rounds, events=events,
                 selected_before=len(selected), selected_after=len(result),
                 preserved=sorted(set(selected) - result.keys()), fixed_point=True)
    return result, proof
