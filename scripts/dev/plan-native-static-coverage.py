#!/usr/bin/env python3
"""Verify source selection and real old/new bake coverage; emit no native pack.

Without actual additional-entry receipts this is explicitly a pending plan.
The original strict batch validator remains authoritative; old receipts cannot
stand in for new source-manifest/input/declaration identities.
"""
import argparse
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import native_static_selection as policy
import native_bulk_producer_gate as gate
import native_linear_acceptance as acceptance

PRODUCER = '8064b80022370de2832964ee22015539441981e7272270b0ff01f5b6c6062ca3'
OLD_ROOT_REVIEW = '825ef66f0d3256b15fa9e7f94e58ca4ec89b26030c40a5b9f89a8782f9e73582'


class Hashes:
    def __init__(self):
        self.files = {}

    def file(self, path):
        # Cache only while inode/size/timestamps remain unchanged.
        path = Path(path).resolve(); st = path.stat()
        token = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)
        old = self.files.get(str(path))
        if old is None or old[0] != token:
            value = policy.sha(path)
            after = path.stat()
            policy.require(token == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                           'input changed while hashing')
            self.files[str(path)] = token, value
        return self.files[str(path)][1]

    def record(self, item):
        path = Path(item['path']).resolve()
        policy.require(self.file(path) == item['sha256'], 'record hash differs')
        if 'bytes' in item:
            policy.require(path.stat().st_size == item['bytes'], 'record size differs')
        return path


def source_selection(prepared_path, expected_sha):
    policy.require(policy.sha(prepared_path) == expected_sha, 'new prepared manifest changed')
    prepared = json.loads(prepared_path.read_text())
    policy.require(prepared['schema'] == 1 and prepared['complete'] is True and
                   prepared['selection']['kind'] == policy.KIND, 'new source preparation incomplete')
    select = prepared['selection']
    policy.require(policy.sha(policy.pinned(select['policy_tool'])) == policy.sha(Path(policy.__file__)),
                   'selection policy source differs; reprepare or explicitly requalify')
    spec = importlib.util.spec_from_file_location('cold_resource', Path(__file__).with_name('prepare-native-image-resource.py'))
    resource = importlib.util.module_from_spec(spec); spec.loader.exec_module(resource)
    src = prepared['source_context']
    archives = list(src['verified_archives'].values())
    compositions = {item['composition']['path'] for item in archives if 'composition' in item}
    policy.require(len(compositions) <= 1, 'multiple source compositions')
    args = SimpleNamespace(romfs=Path(src['original_build_manifest']['path']).parents[1],
        pack_index=policy.pinned(src['source_index']), data_dir=Path(archives[0]['archive']['path']).parent,
        pack_manifest=[policy.pinned(item['manifest']) for item in archives if 'manifest' in item],
        composition_report=Path(next(iter(compositions))) if compositions else None)
    context = resource.SourceContext(args)
    policy.require(context.cache_info == src['source_cache'], 'actual CASC source cache differs')
    closure = json.loads(policy.pinned(select['array_closure']).read_text())['array_format_closure']
    arrays = {name.lower() for group in closure['groups'] for name in group['ordered_members']}
    names, selection = policy.explicit(Path(select['logical_manifest']['path']), select['logical_manifest']['sha256'], context, arrays)
    policy.require(select['array_closure'] == json.loads(policy.pinned(select['reuse_source_pack']).read_text())['selection']['array_closure'], 'selected array census differs')
    policy.require(names == [entry['logical'] for entry in prepared['entries']] and
                   len(names) == select['expected_count'], 'prepared selection coverage differs')
    provenance = json.loads(policy.pinned(src['full_astc_provenance']).read_text())
    # Original artifact root is an explicit recorded input, not inferred from CWD.
    policy.quality_gate(provenance, names, Path(select['artifact_root']))
    entries = context.entries
    for entry in prepared['entries']:
        declaration = json.loads(policy.pinned(entry['source']).read_text())
        policy.require(declaration['source_descriptor'] == entries[entry['logical']] and
                       declaration['source_cache'] == context.cache_info and declaration['layers'] == 1 and
                       declaration['depth'] == 1 and declaration['layout_roundtrip_useful_blocks_exact'] is True,
                       'prepared entry source/layout proof differs')
        for key in ['texture', 'input', 'prepare_report']:
            policy.pinned(entry[key])
        data = Path(entry['texture']['path']).read_bytes()
        original, source_info = context.read(entry['logical'])
        policy.require(data == original and declaration['source_texture'] == source_info, 'prepared texture differs from original CASC membership')
        policy.require(entry['id'] == resource.sha(entry['logical'].encode()), 'prepared logical identity differs')
        desc = declaration['source_descriptor']
        _, _, _, offsets = resource.repack.full_texture(data, desc, declaration['NVN_format'])
        policy.require(offsets[-1] <= resource.MAX_BYTES and entry['input']['bytes'] <= resource.MAX_BYTES,
                       'prepared payload exceeds protocol bound')
    return prepared


def plan(a):
    policy.require(not a.output.exists(), 'new output only')
    new = source_selection(a.prepared, a.prepared_sha256)
    old_record = new['selection']['reuse_source_pack']
    old_path = policy.pinned(old_record); old = json.loads(old_path.read_text())
    hashes = Hashes()
    policy.require(hashes.file(a.old_root_review) == OLD_ROOT_REVIEW, 'exact actual old6217 root review required')
    root = json.loads(a.old_root_review.read_text())
    policy.require(root['status'] == 'ROOT_REVIEWED_ACTUAL_F830_ALL6217_ORIGINAL_PRODUCER_COMPLETE_NOT_GAME'
                   and root['entries'] == 6217 and root['cleanup_complete'] and root['original_unchanged'],
                   'old root review incomplete')
    result = json.loads(Path(root['actual_result']).read_text())
    policy.require(hashes.file(root['actual_result']) == root['result_sha256'] and
                   result['prepare_manifest_sha256'] == old_record['sha256'], 'old source/actual identity differs')
    accepted = acceptance.acceptance(a.functional_review, acceptance.ROOT_SHA, hashes)
    ready = gate.source_ready_gate(a.producer_ready, a.producer_ready_sha256, PRODUCER, accepted, hashes)
    index_path = Path(root['actual_receipt_index'])
    query = Path(json.loads(index_path.read_text())['results'][0]['path']).parent / 'items' / old['batches'][0]['entry_ids'][0] / 'native-resource.json'
    old_coverage = gate.actual_batches_gate(index_path, root['receipt_index_sha256'], ready, PRODUCER,
                                          old, old_record['sha256'], accepted, query, hashes)
    new_coverage = {}
    if a.actual_new_index:
        policy.require(a.actual_new_index_sha256 is not None, 'new actual index hash required')
        idx = json.loads(a.actual_new_index.read_text())
        new_query = Path(idx['results'][0]['path']).parent / 'items' / new['batches'][0]['entry_ids'][0] / 'native-resource.json'
        new_coverage = gate.actual_batches_gate(a.actual_new_index, a.actual_new_index_sha256, ready, PRODUCER,
                                               new, a.prepared_sha256, accepted, new_query, hashes)
    elif a.actual_new_index_sha256:
        raise ValueError('new actual hash requires index')
    complete = bool(new_coverage)
    output = {'schema': 1, 'status': 'ACTUAL_SOURCE_COVERAGE_ONLY_NOT_COMPOSITION_OR_GAME_ACCEPTANCE' if complete else
              'WAIT_ACTUAL_F830_BAKE_FOR_ADDITIONS', 'old_reused_entries': len(old_coverage),
              'additional_entries': len(new['entries']), 'additional_actual_entries': len(new_coverage),
              'actual_bake_complete': complete, 'native_pack_emitted': False, 'deploy_allowed': False,
              'old_source': old_record, 'new_source': policy.record(a.prepared),
              'old_coverage': old_coverage, 'additional_coverage': new_coverage,
              'verified_inputs': [{'path': path, 'sha256': item[1]} for path, item in hashes.files.items()]}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(output, indent=2) + '\n')
    policy.require(not a.require_actual or complete, 'new entries have no actual bake; native coverage remains pending')
    return output


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True); p.add_argument('--prepared-sha256', required=True)
    p.add_argument('--old-root-review', type=Path, required=True)
    p.add_argument('--producer-ready', type=Path, required=True); p.add_argument('--producer-ready-sha256', required=True)
    p.add_argument('--functional-review', type=Path, required=True)
    p.add_argument('--actual-new-index', type=Path); p.add_argument('--actual-new-index-sha256')
    p.add_argument('--require-actual', action='store_true'); p.add_argument('--output', type=Path, required=True)
    print(json.dumps({k: v for k, v in plan(p.parse_args()).items() if k not in ['old_coverage', 'additional_coverage', 'verified_inputs']}, indent=2))
