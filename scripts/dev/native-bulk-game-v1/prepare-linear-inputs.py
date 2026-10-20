#!/usr/bin/env python3
"""Build a non-executable real-game pair draft from complete host composition."""
import copy,hashlib,json,subprocess,sys
from pathlib import Path
R=Path('/home/dl/d2r-mobile')
def check(v,m):
 if not v:raise ValueError(m)
def record(p):
 p=Path(p).resolve();return {'path':str(p),'sha256':hashlib.file_digest(p.open('rb'),'sha256').hexdigest()}
def main():
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--composition-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();check(not a.output.exists(),'new draft output required')
 template=R/'.local/logs/native-general-layout-6bc0b8a-window1m-game-ROOT-inputs.json';check(record(template)['sha256']=='43a3c13b19bcf56088f8893f3cffeef01b7977499f8b893ac8ed4722400c28b5','reviewed 6bc/1MiB template changed');d=json.loads(template.read_text());check(d['runtime_commit']=='6bc0b8ad47bb5481ebfa6d2c41aa0e6a3d619563' and d['pool_window_bytes']==1048576,'runtime/window template changed')
 receipt_path=a.composition_root/'offline-composition-receipt.json';receipt=json.loads(receipt_path.read_text());check(receipt['actual_complete'] and receipt['native_payload_bytes']==443588608 and receipt['driver_sha256']=='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce','actual f830 composition not complete')
 comp_path=a.composition_root/'native-overlay/composition.json';comp=json.loads(comp_path.read_text());check(record(comp_path)=={k:v for k,v in receipt['composition'].items()if k!='bytes'},'composition receipt SHA differs')
 check(comp['complete'] and comp['entry_count']==6217 and not comp['game_acceptance'] and not comp['deploy_allowed'] and comp['selected_logicals_sha256']==d['selected_logicals_sha256'],'different resource selection or acceptance scope')
 d['driver']=record(R/'.local/build/turnip-native-astc-whole-linear-v1/source-ready-freeze/driver/libvulkan_freedreno.so')
 native=d['phases'][1];check(native['kind']=='native','matched phase order changed');native['composition']=record(comp_path);native['overlay_root']=str((a.composition_root/'native-overlay/romfs_overlay').resolve())
 native['overlay']={leaf:record(Path(native['overlay_root'])/leaf)['sha256']for leaf in native['overlay']};native['native_table']=record(a.composition_root/'native-overlay/files/native-layouts.bin')
 check(native['native_table']['sha256']==comp['native_table_sha256'],'native table differs')
 d['native_provider_acceptance']=record(R/'.local/logs/native-astc-linear-NVN-root-actual-review.json');d['native_filter_acceptance']=record(R/'.local/logs/native-astc-linear-filter-reference-q-v1-root-actual-review.json')
 for p in [R/'.local/logs/native-astc-linear-bulk-q-all6217-v1-root-actual-review.json',receipt_path,comp_path]:d['prerequisites'].append(record(p))
 controller=R/'.local/worktrees/native-astc-linear-game-pair-v1/scripts/dev/native-bulk-game-v1/run-game.py';helper=controller.with_name('capture-official-cpu.py');d['performance_tools']['cpu_helper']=record(helper)
 d['execution_ready']=False;d['current_experiment']='f830 wholly-linear nativeHOST ASTC vs same6217 ordinary NVN ASTC4, fixed.60 target60/1MiB window; original6bc canonical/raw unchanged';d['pending_root_review']='Only root can publish separate execution_ready input after reviewing new6217 overlay/table/identity/baseline. Finite fixtures/host composition are not game/bulk visual or performance acceptance.'
 a.output.mkdir(parents=True);inputs=a.output/'inputs-DRAFT.json';inputs.write_text(json.dumps(d,indent=2)+'\n');future_report=R/'.local/logs/native-astc-linear-6bc0b8a-window1m-game-v1/result.json';command=[sys.executable,str(controller),'--inputs',str(inputs),'--inputs-sha256',record(inputs)['sha256'],'--report',str(future_report)]
 result=subprocess.run(command,capture_output=True,text=True);(a.output/'offline-plan.stdout.txt').write_text(result.stdout);(a.output/'offline-plan.stderr.txt').write_text(result.stderr);check(result.returncode==0,'draft source/identity proof failed: '+result.stderr)
 manifest={'status':'F830_REAL_GAME_PAIR_DRAFT_PROOF_PASSED_NOT_EXECUTION_READY','draft_inputs':record(inputs),'composition_receipt':record(receipt_path),'controller':record(controller),'template':record(template),'offline_command':command,'root_next':'Root reviews and writes a separate final input with execution_ready=true, rehashes it and supplies that SHA; this draft will refuse --execute.','no_ADB':True,'game_binary_modified':False};(a.output/'draft-ready.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
