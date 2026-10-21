#!/usr/bin/env python3
"""Reuse old actual bakes + actual additions in original packer/CASC composer.

Source-only draft by default. No new GPU receipt can be synthesized here.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import native_static_selection as policy
import native_static_union as union

ROOT = Path(__file__).resolve().parents[2]
PACKER_SHA = 'f4ae133c09071606a48fce987ac9c1da442266e89d217a386c471eea8ee1b5cb'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def assemble(old_record, old, new_record, new, old_index, new_index):
    value = {'schema': 1, 'complete': True, 'contract': 'disjoint sources; not a synthetic actual batch receipt',
             'selection': {'kind': union.KIND, 'formats': [81, 95], 'array_closure': old['selection']['array_closure']},
             'source_context': old['source_context'], 'entries': old['entries'] + new['entries'],
             'source_parts': [{'role': 'historical', 'prepared': old_record, 'actual_index': old_index},
                              {'role': 'additions', 'prepared': new_record, 'actual_index': new_index}]}
    return value


def run(a):
    require = policy.require
    require(not a.output.exists(), 'new output directory required')
    source = load('explicit_source_plan', ROOT / 'scripts/dev/plan-native-static-coverage.py')
    new = source.source_selection(a.additional_prepared, a.additional_prepared_sha256)
    old_record = new['selection']['reuse_source_pack']; old = json.loads(policy.pinned(old_record).read_text())
    draft = {'status': 'SOURCE_ONLY_WAIT_ACTUAL_ADDITIONS', 'old_entries': len(old['entries']),
             'additional_entries': len(new['entries']), 'native_payload_created': False,
             'composition_complete': False, 'deploy_allowed': False}
    if not a.compose:
        return draft
    require(a.additional_actual_index is not None and a.additional_actual_index_sha256 is not None,
            'composition requires actual additional f830 receipts; source-only preparation is insufficient')
    # Original plan invokes unchanged strict per-batch/source/UID/driver/wire gates.
    # The temporary JSON is a verifier output, not an actual GPU receipt.
    with tempfile.TemporaryDirectory(prefix='dm-native-union-proof-') as temp:
        verified = source.plan(SimpleNamespace(prepared=a.additional_prepared, prepared_sha256=a.additional_prepared_sha256,
            old_root_review=a.old_root_review, producer_ready=a.producer_ready,
            producer_ready_sha256=a.producer_ready_sha256, functional_review=a.functional_review,
            actual_new_index=a.additional_actual_index, actual_new_index_sha256=a.additional_actual_index_sha256,
            require_actual=True, output=Path(temp)/'coverage.json'))
        require(verified['actual_bake_complete'] and verified['additional_actual_entries'] == len(new['entries']),
                'actual addition coverage incomplete')
        proof_bytes = (Path(temp)/'coverage.json').read_bytes()
    old_review = json.loads(a.old_root_review.read_text())
    old_index = {'path': old_review['actual_receipt_index'], 'sha256': old_review['receipt_index_sha256']}
    new_index = policy.record(a.additional_actual_index)
    require(new_index['sha256'] == a.additional_actual_index_sha256, 'new actual index changed')
    merged = assemble(old_record, old, policy.record(a.additional_prepared), new, old_index, new_index)
    composer = load('explicit_union_composer', ROOT / 'scripts/dev/compose-bulk-turnip-native-overlay.py')
    hashes = composer.Fingerprints()
    union.parts(merged, hashes)
    require(policy.sha(a.command_template) == a.command_template_sha256, 'original composer template changed')
    command = json.loads(a.command_template.read_text())
    require(len(command) > 2 and Path(command[1]).name == 'compose-bulk-turnip-native-overlay.py', 'not original composer command')
    # Resolve historical relative artifact paths against their explicit artifact root.
    flags = {command[n]: command[n+1] for n in range(2,len(command),2)}
    require(len(flags) == (len(command)-2)//2 and '--out' in flags, 'malformed original command')
    for flag in ['--raw','--ui-overlay','--ui-proof','--packer','--driver-manifest','--acceptance','--artifact-root']:
        require(flag in flags, 'original composition input absent')
        path = Path(flags[flag]); flags[flag] = str(path if path.is_absolute() else a.artifact_root/path)
    packer = ROOT / 'scripts/dev/pack-native-texture.py'
    require(policy.sha(packer) == PACKER_SHA, 'original strict packer changed')
    a.output.mkdir(parents=True)
    (a.output/'actual-source-coverage.json').write_bytes(proof_bytes)
    manifest = a.output/'union-source.json'; manifest.write_text(json.dumps(merged,indent=2)+'\n')
    refs = a.output/'independent-actual-receipt-references.json'
    refs.write_text(json.dumps({'schema':1,'kind':union.ACTUAL_KIND,'source_parts':merged['source_parts']},indent=2)+'\n')
    all_coverage = {**verified['old_coverage'], **verified['additional_coverage']}
    specs = [{'source':e['source']['path'],'texture':e['texture']['path'],'baked':all_coverage[e['id']]['baked']}
             for e in merged['entries']]
    entries = a.output/'entries.json'; entries.write_text(json.dumps(specs,indent=2)+'\n')
    query = Path(verified['additional_coverage'][new['entries'][0]['id']]['metadata'])
    pack = a.output/'native-pack'
    pack_command = [sys.executable,str(packer),'--entries',str(entries),'--driver-manifest',flags['--driver-manifest'],
                    '--driver-query',str(query),'--output',str(pack)]
    (a.output/'packer-command.json').write_text(json.dumps(pack_command,indent=2)+'\n')
    subprocess.run(pack_command,check=True,stdout=(a.output/'packer.stdout.txt').open('w'),stderr=subprocess.STDOUT)
    # Same old IDs/order/native bytes: retain old markers and family table entries.
    old_pack = Path(flags['--native-pack'])
    if not old_pack.is_absolute(): old_pack = a.artifact_root/old_pack
    original_record = json.loads((old_pack/'native-pack.json').read_text())
    combined_record = json.loads((pack/'native-pack.json').read_text())
    require(combined_record['entries'][:len(old['entries'])] == original_record['entries'],
            'old native resources changed during strict reproduction')
    replacements = {'--native-pack':str(pack),'--entries':str(entries),'--entries-sha256':policy.sha(entries),
        '--packer':str(packer),'--driver-query':str(query),'--source-pack-manifest':str(manifest),
        '--source-pack-manifest-sha256':policy.sha(manifest),'--producer-source-ready':str(a.producer_ready),
        '--producer-source-ready-sha256':a.producer_ready_sha256,'--actual-batch-receipt-parts':str(refs),
        '--actual-batch-receipt-parts-sha256':policy.sha(refs),'--out':str(a.output/'native-overlay')}
    flags.pop('--actual-batch-receipts'); flags.pop('--actual-batch-receipts-sha256')
    flags.update(replacements)
    compose_command = [sys.executable,str(ROOT/'scripts/dev/compose-bulk-turnip-native-overlay.py')]
    for flag,value in flags.items(): compose_command += [flag,value]
    (a.output/'composer-command.json').write_text(json.dumps(compose_command,indent=2)+'\n')
    subprocess.run(compose_command,check=True,stdout=(a.output/'composer.stdout.txt').open('w'),stderr=subprocess.STDOUT)
    composition = json.loads((a.output/'native-overlay/composition.json').read_text())
    require(composition['complete'] and composition['entry_count'] == len(merged['entries']) and
            composition['upstream_UI_preserved'] and not composition['deploy_allowed'] and
            not composition['game_acceptance'], 'combined composition closure differs')
    receipt = {'schema':1,'status':'OFFLINE_COMPOSITION_WITH_INDEPENDENT_ACTUAL_BAKES_NOT_GAME_ACCEPTANCE',
        'composition':policy.record(a.output/'native-overlay/composition.json'),'source_union':policy.record(manifest),
        'independent_actual_references':policy.record(refs),'old_payloads_reused_without_rebake':len(old['entries']),
        'additional_actual_entries':len(new['entries']),'old_native_markers_and_entry_metadata_unchanged':True,
        'game_binary_modified':False,'game_acceptance':False,'deploy_allowed':False}
    (a.output/'offline-composition-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--additional-prepared',type=Path,required=True);p.add_argument('--additional-prepared-sha256',required=True)
    p.add_argument('--additional-actual-index',type=Path);p.add_argument('--additional-actual-index-sha256')
    p.add_argument('--old-root-review',type=Path,required=True)
    p.add_argument('--producer-ready',type=Path,required=True);p.add_argument('--producer-ready-sha256',required=True)
    p.add_argument('--functional-review',type=Path,required=True)
    p.add_argument('--command-template',type=Path,required=True);p.add_argument('--command-template-sha256',required=True)
    p.add_argument('--artifact-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compose',action='store_true')
    print(json.dumps(run(p.parse_args()),indent=2))
