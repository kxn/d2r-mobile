#!/usr/bin/env python3
"""One exact readonly-vec4 compiler transition; no rebake or runtime override.

The original opaque0e pack and previously accepted12b8 provenance remain
separate from this82f consumer. Only an authenticated actual producer/data run
and exact source/build lineage permit the original consumer-table writer.
"""
import argparse,hashlib,importlib.util,json,os,subprocess
from pathlib import Path
OLD='12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5'
NEW='82f29febf953f9e3c4891ce3d793c171366af965ef1b25a2fbdb1738d725f3cb'
OLD_BUILD='424afe4f84f13a2303fc4dd6f0c6d20af65b2931d3697d55fef3e627a036aba1'
NEW_BUILD='c6a3804a154567a2c659c7c839374642f44094d3c3589a28661e6be92b2dddc8'
DATA_BUILD='d5e27278003d0dd5218b8dc34a2bfd4a42919a5f71a765c9aaf4fe254ca53337'
DATA_BRIDGE='44944eb8131fbc08eb92f7c86b8a86e296f1b327f44b433034f59b5b5cc063da'
PRODUCER='9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa'
APK='89a4dd163f32ec3095b7066233e31e6ea52e80377411ad7847d1d3b8134ec813'
UUIDS=('layout_UUID','device_UUID','driver_UUID')
def require(ok,msg):
 if not ok:raise ValueError(msg)
def sha(path):
 with Path(path).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def module(path,name):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m

def transition(root):
 a=root/'.local/build/turnip-internal-queue-sync-v1';b=root/'.local/build/turnip-readonly-ssbo-route-v1';require(sha(a/'build.json')==OLD_BUILD and sha(b/'build.json')==NEW_BUILD,'frozen driver builds changed');old=json.load(open(a/'build.json'));new=json.load(open(b/'build.json'))
 for x,h in [(old,OLD),(new,NEW)]:require(x['complete']and x['source_reverified']and x['driver']['sha256']==h and sha(x['driver']['path'])==h,'actual driver ELF/build binding')
 ledgers=[];helper=root/'tools/experiments/turnip-native-bc3/build.py';require(sha(helper)=='3fb441cd0db7cf4072461a4e445106ad505c7197db2877accff9f06068818491','original full-source verifier fingerprint');h=module(helper,'original_source_ledger')
 for p,x in [(a,old),(b,new)]:
  ledger=p/'candidate-source-ledger.json';require(sha(ledger)==x['candidate_source_ledger_sha256'],'source ledger digest');d=json.load(open(ledger));require(h.manifest(p/'mesa-26.2.5')==d,'complete source bytes changed');ledgers.append(d)
 delta=[k for k in sorted(ledgers[0].keys()|ledgers[1].keys())if ledgers[0].get(k)!=ledgers[1].get(k)];require(len(ledgers[0])==len(ledgers[1])==18218 and delta==['src/freedreno/ir3/ir3_compiler_nir.c'],'only exact IR3 route delta permitted')
 require(sha(b/'readonly-vec4-uav.patch')==new['policy']['patch_sha256']=='4df03cae833b4b709d3349c08e753c64d6cf1dfeb5e4900ef0351e54b7b87853','approved route patch')
 require(old['declared_environment']==new['declared_environment']and old['tools']==new['tools'],'compiler environment/tools changed')
 aa=[s.replace(str(a),'<ROOT>')for s in old['setup_argv']];bb=[s.replace(str(b),'<ROOT>')for s in new['setup_argv']];require(bb==aa+['-Dtu-build-id='+new['tu_build_id_hex']],'unexpected Meson option delta')
 require((b/'android-aarch64.ini').read_text()==(a/'android-aarch64.ini').read_text().replace("c_args = ['-fPIC']","c_args = ['-fPIC', '-DDM_IR3_READONLY_VEC4_UAV=1']"),'unexpected cross configuration')
 for n,k in [('actual-emitter-compile-command.json','actual_emitter_compile_command_sha256'),('actual-cache-compile-command.json','actual_cache_compile_command_sha256')]:require(sha(b/n)==new[k],'actual compile-command binding')
 return {'baseline_build_sha256':OLD_BUILD,'consumer_build_sha256':NEW_BUILD,'full_members':18218,'only_source_delta':delta,'patch_sha256':new['policy']['patch_sha256'],'source_ledgers_reexecuted':True,'cache_id_separate_from_image_layout':True,'image_host_import_FDL_WSI_sources_unchanged':True}

def uuids_match(old,new):
 for k in UUIDS:
  v=new.get(k);require(isinstance(v,str)and len(v)==32 and any(bytes.fromhex(v))and v==old[k],'actual '+k+' differs')

def authenticate(root,capture,capture_sha):
 p=capture/'capture-receipt.json';require(p.is_file()and sha(p)==capture_sha,'missing or wrong explicit capture receipt');r=json.load(open(p));require(r['APK_sha256']==APK and r['driver_sha256']==NEW and r['ordinary_uid']>=10000 and r['terminal_rc']==0 and r['bits_summaries_zero_mismatch']==8 and r['game_modules_executed']is False,'actual data capture incomplete')
 for n,h in r['outputs'].items():require(sha(capture/n)==h,'captured file hash: '+n)
 log=(capture/'device/files/data.log').read_text();pid=int(r['pid']);require('TEXELDATA_BEGIN pid='+str(pid)+' uid='+str(r['ordinary_uid'])in log and 'TEXELDATA_TERMINAL rc=0'in log and 'TEXELDATA_CHECKS passed=1234'in log,'actual cold process/terminal')
 require('source_sha256='+PRODUCER in log and 'TEXELDATA_PRODUCER_END rc=0'in log,'original producer did not finish')
 rows=[json.loads(s)for s in log.splitlines()if s.startswith('{')];q=[x for x in rows if x.get('native_device_identity')];require(len(q)==1 and q[0]['driverID']==18,'actual single Turnip query')
 done=[x for x in rows if x.get('test')=='native-host-image-payload'];require(len(done)==1 and done[0]['result']=='passed'and done[0]['formats']==2 and done[0]['checks']==150,'owned BC3 host-copy smoke incomplete')
 maps=(capture/'maps.txt').read_text();require('/org.kxn.hdrdata/files/libvulkan_freedreno.so'in maps or '/org.kxn.hdrdata/files/libvulkan_freedreno.so'.replace('/org.kxn.hdrdata/','/0/org.kxn.hdrdata/')in maps,'actual private custom driver map missing');require(sha(capture/'device/files/libvulkan_freedreno.so')==NEW,'pulled actual mapped path driver hash')
 build=root/'.local/build/turnip-readonly-ssbo-route-v1/data-fixture-v5/build.json';require(sha(build)==DATA_BUILD,'frozen data fixture build record');b=json.load(open(build));require(b['APK']['sha256']==APK and sha(b['APK']['path'])==APK and b['producer']['source_sha256']==PRODUCER,'source-bound fixture build');require(sha(build.parent/'actual-producer-compile-command.json')==b['producer']['actual_compile_command_sha256'],'source919 actual compile command')
 for name,h in b['fixture_payloads'].items():require(sha(capture/'device/files/shaders'/name)==h,'actual shader source payload')
 bridge=capture/'data-native-bridge.json';require(sha(bridge)==DATA_BRIDGE,'frozen actual data/native bridge');d=json.load(open(bridge));require(d['capture_receipt_sha256']==capture_sha and d['actual_driver_sha256']==NEW and d['all116_actual_shader_payloads_verified']and d['NVN_marker_checks']==1234,'native/data bridge receipt binding')
 for path,h in d['inputs'].items():require(sha(path)==h,'native bridge input digest')
 coverage={(x['marker'],x['representation']):x for x in d['cases']};require(all(coverage[x]['every_actual_variant_has_one_vec4_LDIB']for x in [(53,'alternative-spec'),(54,'alternative-base')]),'data tests did not execute vector route')
 return q[0],{'capture_sha256':capture_sha,'capture_path':str(capture.resolve()),'data_log_sha256':sha(capture/'device/files/data.log'),'maps_sha256':sha(capture/'maps.txt'),'ordinary_uid':r['ordinary_uid'],'pid':pid,'fixture_build_sha256':sha(build),'native_data_bridge_sha256':sha(bridge),'actual_producer_compiled_source':PRODUCER,'actual_query_extracted_from_original_producer_stdout_not_a_relabelled_resource_bake':True}

def parser_cases(base,out,newtable,oldtable,identity,producer,pack):
 binary,record=base.compile_parser(out);refs={}
 for name,data in [('new',identity+bytes.fromhex(NEW)+producer),('old',identity+bytes.fromhex(OLD)+producer),('wrong',bytes([identity[0]^1])+identity[1:]+bytes.fromhex(NEW)+producer),('pack',pack)]:
  p=out/(name+'.identity');p.write_bytes(data);refs[name]=p
 bad=bytearray(newtable.read_bytes());bad[-1]^=1;corrupt=out/'negative-corrupt-table.bin';corrupt.write_bytes(bad);results={}
 for name,p,who,want in [('new_accepts',newtable,'new',1),('old_rejects_new',newtable,'old',0),('new_rejects_old',oldtable,'new',0),('wrong_UUID_rejected',newtable,'wrong',0),('bad_digest_rejected',corrupt,'new',0)]:
  c=[str(binary),str(p),str(refs[who]),str(refs['pack']),str(want)];v=subprocess.run(c,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True);results[name]=json.loads(v.stdout)
 corrupt.unlink();record['cases']=results;return record

def run(root,capture,capture_sha,out):
 require(not out.exists(),'new output required; do not overwrite provenance');actual,auth=authenticate(root,capture,capture_sha);lineage=transition(root);base=module(root/'tools/experiments/native-layout-cache-compat/rebind.py','original_native_compat')
 prior_path=root/'.local/build/native-bc3-linear-consumer-12b8-v1/compatibility.json';prior=json.load(open(prior_path));require(prior['accepted']and prior['consumer_driver_sha256']==OLD and prior['producer_source_sha256']==PRODUCER,'previous12b8 cache compatibility');uuids_match(prior['actual_UUIDs'],actual)
 for name,h in prior['tool_inputs'].items():require(sha(root/name)==h,'original compatibility tool source changed')
 oldconsumer=base.pin(prior['new_table']);mp=base.pin(prior['original_provenance']['manifest']);manifest=json.load(open(mp));source=mp.parent/'native-layouts.bin';require(sha(source)==manifest['table_sha256'],'original0e source table digest');data=source.read_bytes();require(data[160:]==oldconsumer.read_bytes()[160:],'original source recipe/family differs from accepted12b8 consumer');require(manifest['pack_content_sha256']==prior['original_pack_content_sha256']and manifest['producer_source_sha256']==PRODUCER,'opaque source content provenance')
 new=base.rewrite_table(data,manifest,NEW);out.mkdir(parents=True);table=out/'consumer-native-layouts.bin';table.write_bytes(new);identity=bytes.fromhex(''.join(actual[k]for k in UUIDS));producer=bytes.fromhex(PRODUCER);parser=parser_cases(base,out,table,oldconsumer,identity,producer,data[32:64]);require(new[160:]==data[160:] and new[:64]==data[:64]and new[96:128]==data[96:128],'only consumer driver/digest fields may change')
 result={'schema':1,'contract':'DM exact readonly-vec4 compiler host-layout compatibility v1','accepted':True,'consumer_table_generated':True,'rebaked':False,'source_bake_driver_sha256':prior['source_bake_driver_sha256'],'previous_consumer_driver_sha256':OLD,'consumer_driver_sha256':NEW,'original_pack_content_sha256':prior['original_pack_content_sha256'],'producer_source_sha256':PRODUCER,'actual_UUIDs':{k:actual[k]for k in UUIDS},'consumer_query_authentication':auth,'source_transition':lineage,'original_compatibility':{'path':str(prior_path),'sha256':sha(prior_path)},'original_pack_provenance_preserved':True,'original_payloads_not_reexported_or_rehashed_in_this_step':True,'new_table':{'path':str(table.resolve()),'sha256':sha(table)},'table_entry_family_body_unchanged':True,'production_parser':parser,'game_or_native_NVN_image_coherence_executed':False,'pending_runtime_gates':['qualified exact native_resident driver source admission without changing parser','ordinary game visual/driver/shader/native resource guard and controlled performance'],'tool_inputs':{str(Path(__file__).resolve()):sha(__file__),str(root/'tools/experiments/turnip-native-bc3/build.py'):sha(root/'tools/experiments/turnip-native-bc3/build.py'),str(root/'tools/experiments/native-layout-cache-compat/rebind.py'):sha(root/'tools/experiments/native-layout-cache-compat/rebind.py')}}
 (out/'compatibility.json').write_text(json.dumps(result,indent=2)+'\n');return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[3]);p.add_argument('--capture',type=Path,required=True);p.add_argument('--capture-receipt-sha256',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=run(a.repo.resolve(),a.capture.resolve(),a.capture_receipt_sha256,a.output.resolve());print(json.dumps({'accepted':r['accepted'],'new_table':r['new_table'],'rebaked':False}))
if __name__=='__main__':main()
