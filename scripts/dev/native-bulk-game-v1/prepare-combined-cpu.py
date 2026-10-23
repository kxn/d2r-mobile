#!/usr/bin/env python3
"""Prepare ONE original CPU round. Host-only; never launches a device/batch."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent
R=Path('/home/dl/d2r-mobile')
spec=importlib.util.spec_from_file_location('combined_cpu_runner',HERE/'run-game.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
PARENT='52181dc6b7cbdb7df7168d62201a84dd27d05672'
PARENT_REVIEW=R/'.local/logs/descriptor-pool-block-512-52181dc-root-canonical-raw-review.json'
TEMPLATE=R/'.local/build/private-image-budget-74d7e17-Q735-pair-v1/root-execute/PARENT1.json'
STOPPED_REVIEW=R/'.local/logs/three-route-stopped-baseline-v1/root-review.json'
DELIVERED=R/'.local/logs/descriptor-pool-52181dc-desktop-Q735-v1/delivered-baseline.json'

def record(path):
    path=Path(path).resolve()
    return {'path':str(path),'sha256':g.sha(path),'bytes':path.stat().st_size}

def make_input(round_name,output,candidate_review=None,execution_ready=False):
    g.require(round_name in ('PARENT1','CANDIDATE1','CANDIDATE2','PARENT2'),'ABBA round differs')
    delivered=g.read(DELIVERED);stopped=g.read(STOPPED_REVIEW)
    g.require(g.pinned(stopped['delivered_baseline'])==DELIVERED
        and stopped['runtime']==PARENT and stopped['status']=='ROOT_REVIEWED_CURRENT52181_STOPPED_LIVE_BASELINE_TEMP_ONLY'
        and all(stopped[k] is True for k in ('game_PID_absent','same_delivery','immutable_identity_unchanged',
            'only_temp_changed','original_Device_identity_passed','controller_lock_absent')),
        'actual stopped current52181 review required')
    fresh=g.read(g.pinned(stopped['fresh_baseline']))
    for key in ('serial','package','APK_sha256','host_ELF_SHA256','chain','profile','publication_receipt'):
        g.require(fresh[key]==delivered[key],'fresh current52181 identity differs '+key)
    parent=g.read(PARENT_REVIEW)
    g.require(parent['runtime']==PARENT and parent['APK']['sha256']==delivered['APK_sha256']
        and parent['host_ELF_SHA256']==delivered['host_ELF_SHA256'],'actual current52181 parent differs')
    is_parent=round_name.startswith('PARENT')
    g.require(is_parent or candidate_review is not None,'candidate requires actual root canonical/raw review; no pending approval')
    selected=PARENT_REVIEW if is_parent else Path(candidate_review).resolve()
    card=g.read(selected)
    g.require(card['status']=='ROOT_REVIEWED_HOST_RESOURCE_PREPARATION_CANONICAL_AND_FRESH_RAW28'
        and card['host_only_changes'] is True and card['game_logic_unchanged'] is True
        and card['tool_ready'] is True and card['actual_device_evidence'] is False
        and (is_parent or (card['base']==PARENT and card['runtime']!=PARENT)),
        'actual parent/combined source and artifact review required')
    p=g.read(TEMPLATE)
    for key in ('native_memory_validation','builtin_vma_on_suspend','official_l1i_record'):
        p.pop(key,None)
    p.update(execution_ready=bool(execution_ready),runtime_commit=card['runtime'],
        canonical=card['canonical_ready'],raw=card['raw_ready'],baseline=stopped['fresh_baseline'],
        original_apk=parent['APK'],original_host_elf=parent['host_ELF_SHA256'],
        native_candidate_validation={'root_review':record(selected)},
        native_host_experiment={'kind':'resource-preparation','base_runtime':card['base']},
        official_cpu_aa={'repetitions':3,'record_seconds':30,'sample_frequency_Hz':99,'callgraph':'fp'},
        performance_tools={'cpu_helper':record(HERE/'capture-official-cpu.py'),
            'scheduler_helper':record(HERE.parent/'capture-android-scheduler.py')},
        output=str(output),authorization_scope='One '+round_name+' exact current52181/ROOT-reviewed combined CPU control. '
        'Original3walks/3x30s99HzFP/5x8sPerfetto/Simpleperf; .60fixed/2750x1260/full2838/f830/950/'
        'shared1MiB/lazy1/raw0/spanREFERENCE1/accountingON and ADPFreference retained. Restore actual stopped52181 '
        'with originalsave2/stop/six-directoryCAS. No automatic next round, no historical scope relabel or projected FPS gain.')
    p['prerequisites']+= [record(STOPPED_REVIEW),record(DELIVERED)]
    return p

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--round',choices=('PARENT1','CANDIDATE1','CANDIDATE2','PARENT2'),required=True)
    parser.add_argument('--candidate-review',type=Path)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--execution-ready',action='store_true',help='Explicit root-reviewed input preparation; still never executes')
    args=parser.parse_args();directory=args.directory.resolve();output=args.output.resolve()
    g.require(directory.is_relative_to(R/'.local/build') and output.is_relative_to(R/'.local/logs'), 'private paths required')
    directory.mkdir(parents=True,exist_ok=False)
    p=make_input(args.round,output,args.candidate_review,args.execution_ready)
    path=directory/'inputs.json';path.write_text(json.dumps(p,indent=2)+'\n')
    argv=[sys.executable,str(HERE/'run-game.py'),'--inputs',str(path),'--inputs-sha256',g.sha(path),
          '--report',str(output/'result.json')]
    with (directory/'offline-plan.json').open('w') as stdout,(directory/'offline-stderr.log').open('w') as stderr:
        subprocess.run(argv,check=True,stdout=stdout,stderr=stderr)
    result={'input':record(path),'offline_argv':argv,'root_execute_argv':argv+['--execute'],
        'offline_proof_exit_code':0,'execution_ready':p['execution_ready'],'device_access':False}
    (directory/'ready.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
