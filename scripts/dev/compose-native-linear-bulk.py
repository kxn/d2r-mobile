#!/usr/bin/env python3
"""Offline f830 receipt->original strict packer->CASC composition, no device access."""
import argparse,copy,hashlib,importlib.util,json,subprocess,sys
from pathlib import Path
R=Path('/home/dl/d2r-mobile');HERE=Path(__file__).resolve().parent
P='8064b80022370de2832964ee22015539441981e7272270b0ff01f5b6c6062ca3'
def require(v,m):
 if not v:raise ValueError(m)
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def record(p):p=Path(p).resolve();return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def load(path,name):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def current_ready():
 oldpath=R/'.local/logs/turnip-native-bulk-controller-v2-source-ready.json';require(sha(oldpath)=='1f88055509ac41148b4d6cea1c97b48a119d929c67511be7c401d00ea5b2066c','historical producer ready changed');d=json.loads(oldpath.read_text());old=copy.deepcopy(d)
 controller_tree=R/'.local/worktrees/native-astc-linear-bulk-tools-v1';require(subprocess.check_output(['git','-C',str(controller_tree),'rev-parse','HEAD'],text=True).strip()=='d9e756c6a43f7c03b097d4c3abdf9ae85803159c','current controller revision changed');require(not subprocess.check_output(['git','-C',str(controller_tree),'status','--porcelain'],text=True),'current controller dirty')
 d['historical_compiled_producer_ready']=record(oldpath)
 d['active_driver']=record(R/'.local/build/turnip-native-astc-whole-linear-v1/source-ready-freeze/driver/libvulkan_freedreno.so');d['active_driver_ready']=record(R/'.local/logs/turnip-native-astc-whole-linear-v1-source-ready.json')
 d['ordinary_UID_controller']=copy.deepcopy(old['ordinary_UID_controller']);d['ordinary_UID_controller'].update(record(controller_tree/'scripts/dev/run-turnip-native-resource-batches-v2.py'))
 for name in ['run-turnip-native-resource-batches-v2.py','native_batch_installer.py','native_batch_outputs.py']:
  d['source_files'].append(record(controller_tree/'scripts/dev'/name))
 d['source_tree']=str(controller_tree);d['source_revision']='d9e756c6a43f7c03b097d4c3abdf9ae85803159c';d['active_driver_only_successor']=True;d['canonical_APK_rebuilt_or_changed']=False
 d['status']='UNCHANGED_COMPILED_8064_PRODUCER_WITH_F830_TOOLS_TARGET_NOT_ACTUAL_BATCH_ACCEPTANCE'
 d['historical_driver_scope']='canonical.driver_sha256 retains the original AA compile/sourceReady provenance; active_driver is this separate controller target. Actual receipts, not this source report, must prove f830 was loaded.'
 require(d['canonical']==old['canonical'] and d['producer_source_identity']==old['producer_source_identity'],'historical producer metadata changed');return d

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--actual-output',type=Path,default=R/'.local/logs/native-astc-linear-bulk-q-all6217-v1');p.add_argument('--output',type=Path,required=True);p.add_argument('--compose',action='store_true');a=p.parse_args()
 composer=load(HERE/'compose-bulk-turnip-native-overlay.py','linear_bulk_composer');h=composer.Fingerprints();acceptance_path=R/'.local/logs/native-astc-linear-NVN-root-actual-review.json';acceptance=composer.receipt_gate(acceptance_path,'9089fbe5d64b3842e8ae387506e6a22c9cc2034881a76e2711a675407f05af0f',h)
 ready=current_ready();prepared_path=R/'.local/build/turnip-native-bulk-act1-character-full4-v1-inputs/batch-prepare.json';require(sha(prepared_path)=='e68a8d45226c2820d2b1fc4d3a3f67b62a6c88e0950b86cc7c837a2da07143c8','prepared source changed');prepared=json.loads(prepared_path.read_text())
 plan={'scope':'Source-only plan until all49 actual batches close; no device/game operation','producer_sha256':P,'driver_sha256':acceptance['driver_sha256'],'prepared':record(prepared_path),'actual_root':str(a.actual_output.resolve()),'output':str(a.output.resolve()),'original_packer':record(R/'.local/worktrees/turnip-real-native-family-v1/scripts/dev/pack-native-texture.py'),'old_AA_ready_and_APK_unchanged':True,'actual_complete':False,'expected_entries':6217,'expected_batches':49,'game_acceptance':False,'deploy_allowed':False}
 require(plan['original_packer']['sha256']=='f4ae133c09071606a48fce987ac9c1da442266e89d217a386c471eea8ee1b5cb','original strict packer changed')
 if not a.compose:print(json.dumps(plan,indent=2));return
 result_path=a.actual_output/'result.json';actual=json.loads(result_path.read_text());require(actual['stage']=='actual-turnip-native-resource-batches-owned-passed' and not actual['cleanup_errors'] and actual['original_before']==actual['original_after'] and all(actual[k]for k in ['original_unchanged','fixture_PID_absent','fixture_uninstalled','fixture_tmp_absent','owned_clients_stopped']),'actual batch transaction not complete/clean')
 require(actual['driver_sha256']==acceptance['driver_sha256'] and actual['producer_source_sha256']==P and actual['controller_sha256']==ready['ordinary_UID_controller']['sha256'] and actual['prepare_manifest_sha256']==sha(prepared_path) and len(actual['runs'])==49 and actual['entry_count']==6217 and actual['first_batch']==0 and actual['batch_count']==49,'actual complete target/coverage differs')
 index_path=a.actual_output/'actual-batch-receipt-index.json';require(sha(index_path)==actual['receipt_index_sha256'],'actual index not final published receipt');index=json.loads(index_path.read_text());require(index['entries']==6217 and len(index['results'])==49,'not full6217 actual index')
 query=Path(index['results'][0]['path']).parent/'items'/prepared['batches'][0]['entry_ids'][0]/'native-resource.json'
 require(not a.output.exists(),'new host composition output required');a.output.mkdir(parents=True)
 ready_path=a.output/'current-producer-source-ready.json';ready_path.write_text(json.dumps(ready,indent=2)+'\n');composer.producer_gate.source_ready_gate(ready_path,sha(ready_path),P,acceptance,h)
 coverage=composer.producer_gate.actual_batches_gate(index_path,sha(index_path),ready,P,prepared,sha(prepared_path),acceptance,query,h);require(len(coverage)==6217,'actual full entry coverage missing')
 specs=[{'source':e['source']['path'],'texture':e['texture']['path'],'baked':coverage[e['id']]['baked']}for e in prepared['entries']];entries_path=a.output/'entries.json';entries_path.write_text(json.dumps(specs,indent=2)+'\n')
 driver_manifest=R/'.local/logs/turnip-native-astc-whole-linear-v1-source-ready.json';packer=Path(plan['original_packer']['path'])
 pack_command=[sys.executable,str(packer),'--entries',str(entries_path),'--driver-manifest',str(driver_manifest),'--driver-query',str(query),'--output',str(a.output/'native-pack')]
 (a.output/'packer-command.json').write_text(json.dumps(pack_command,indent=2)+'\n');subprocess.run(pack_command,check=True,stdout=(a.output/'packer.stdout.txt').open('w'),stderr=subprocess.STDOUT)
 command=json.loads((R/'.local/build/turnip-native-bulk-6217-aa185-v1/composer-command.json').read_text());command[0]=sys.executable;command[1]=str(HERE/'compose-bulk-turnip-native-overlay.py')
 replacements={'--native-pack':str(a.output/'native-pack'),'--entries':str(entries_path),'--entries-sha256':sha(entries_path),'--packer':str(packer),'--driver-manifest':str(driver_manifest),'--driver-query':str(query),'--acceptance':str(acceptance_path),'--acceptance-sha256':sha(acceptance_path),'--producer-source-ready':str(ready_path),'--producer-source-ready-sha256':sha(ready_path),'--actual-batch-receipts':str(index_path),'--actual-batch-receipts-sha256':sha(index_path),'--out':str(a.output/'native-overlay')}
 for flag,value in replacements.items():command[command.index(flag)+1]=value
 (a.output/'composer-command.json').write_text(json.dumps(command,indent=2)+'\n');subprocess.run(command,check=True,stdout=(a.output/'composer.stdout.txt').open('w'),stderr=subprocess.STDOUT)
 comp=json.loads((a.output/'native-overlay/composition.json').read_text());control=R/'.local/build/turnip-bulk-act1-char-full4-6217-standard-control-v1/composition.json';require(sha(control)=='6592ab8446f0b655dac004db748b6c1412882294954187b6ed49dca8eb51e305','matched source ASTC control changed');c=json.loads(control.read_text());require(comp['selected_logicals_sha256']==c['selected_logicals_sha256'] and comp['entry_count']==6217 and comp['complete'] and not comp['game_acceptance'] and not comp['deploy_allowed'],'matched composition differs')
 plan.update(actual_complete=True,actual_transaction=record(result_path),actual_index=record(index_path),composition=record(a.output/'native-overlay/composition.json'),matched_standard_control=record(control),source_ready=record(ready_path),native_payload_bytes=comp['native_payload_bytes'],verified_files=h.finish());(a.output/'offline-composition-receipt.json').write_text(json.dumps(plan,indent=2)+'\n');print(json.dumps(plan,indent=2))
if __name__=='__main__':main()
