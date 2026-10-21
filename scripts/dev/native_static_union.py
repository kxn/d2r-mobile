"""Disjoint source/bake union; references real receipts, never fabricates one."""
import json
from pathlib import Path
import native_static_selection as policy

KIND = 'disjoint-historical-and-explicit-native-sources-v1'
ACTUAL_KIND = 'references-to-independent-actual-native-batches-v1'
COMMON = ['original_build_manifest', 'source_index', 'verified_archives', 'source_cache']


def parts(value, hashes):
    selected = value['source_parts']
    policy.require(len(selected) == 2 and [p['role'] for p in selected] == ['historical', 'additions'],
                   'exact historical/additions source parts required')
    result = []
    for part, kind in zip(selected, [policy.LEGACY_KIND, policy.KIND]):
        path = hashes.record(part['prepared'])
        prepared = json.loads(path.read_text())
        policy.require(prepared['complete'] is True and prepared['selection']['kind'] == kind,
                       'incomplete, nested or wrong-kind source part')
        result.append((part, path, prepared))
    old, new = [p[2] for p in result]
    policy.require(new['selection']['reuse_source_pack'] == selected[0]['prepared'],
                   'explicit selection did not pin this historical source')
    for key in COMMON:
        policy.require(old['source_context'][key] == new['source_context'][key], 'source parts have different data provenance')
    policy.require(old['selection']['array_closure'] == new['selection']['array_closure'], 'source parts have different array census')
    entries = old['entries'] + new['entries']
    ids = [e['id'] for e in entries]; names = [e['logical'] for e in entries]
    policy.require(len(set(ids)) == len(ids) and len(set(names)) == len(names), 'source union overlaps')
    policy.require(value['entries'] == entries and value['source_context'] == old['source_context'] and
                   value['selection']['array_closure'] == old['selection']['array_closure'], 'source union coverage/context differs')
    return result


def source_gate(value, expected, hashes, artifact_root, validate_source):
    policy.require(value['schema'] == 1 and value['complete'] is True and value['selection']['kind'] == KIND,
                   'invalid union source manifest')
    validated = parts(value, hashes)
    answers = [validate_source(path, part['prepared']['sha256'], hashes, artifact_root)
               for part, path, _ in validated]
    policy.require(answers[0][1] == answers[1][1] and answers[0][2] == answers[1][2],
                   'source quality/cache differs between parts')
    return value, answers[0][1], answers[0][2]


def contexts(value, hashes):
    return {entry['id']: prepared['source_context'] for _, _, prepared in parts(value, hashes)
            for entry in prepared['entries']}


def actual_coverage(value, references_path, expected_sha, ready, producer, acceptance, driver_query, hashes, strict_gate):
    policy.require(references_path is not None and hashes.file(references_path) == expected_sha,
                   'exact independent actual receipt references required')
    references = json.loads(references_path.read_text())
    policy.require(references == {'schema': 1, 'kind': ACTUAL_KIND,
                                  'source_parts': value['source_parts']}, 'actual receipt references differ from sources')
    covered = {}
    for part, path, prepared in parts(value, hashes):
        index_path = hashes.record(part['actual_index'])
        index = json.loads(index_path.read_text())
        policy.require(index.get('results'), 'part has no actual batches')
        query = Path(index['results'][0]['path']).parent / 'items' / prepared['batches'][0]['entry_ids'][0] / 'native-resource.json'
        actual = strict_gate(index_path, part['actual_index']['sha256'], ready, producer, prepared,
                             part['prepared']['sha256'], acceptance, query, hashes)
        policy.require(not covered.keys() & actual.keys(), 'actual receipt coverage overlaps')
        covered.update(actual)
    policy.require(set(covered) == {e['id'] for e in value['entries']} and
                   str(driver_query.resolve()) in {e['metadata'] for e in covered.values()},
                   'actual source union coverage/query incomplete')
    return covered
