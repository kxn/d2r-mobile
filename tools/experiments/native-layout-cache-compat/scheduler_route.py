#!/usr/bin/env python3
"""Explicit 12b8 -> 82c scheduling-only host-layout cache compatibility.

Authenticate the actual compiler-probe property query, preserve the original
0e53 bake/919 producer and6217 payload provenance, and use the unchanged C
parser. Never rebake, alter runtime parsing, or assert GPU/game qualification.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,subprocess
OLD='12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5'
NEW='82cbd84ea66455292e6761f8667830fc26945d61a5274a145ab12cb52f2ae3dd'
OLD_BUILD='424afe4f84f13a2303fc4dd6f0c6d20af65b2931d3697d55fef3e627a036aba1'
NEW_BUILD='48c3ee6c6ab82c2d5789a5f3423ba3072aef14478abbd6174b0467c17ac90689'
PROBE_BUILD='b5304ebea2d3d3b83e5bfcb78e79761002bbcbbfa5af9001d94baf58903802b1'
APK='c27b0b646044b5f0c78dd586d75bfb07782eccec300644db3d2602cb2f3a6dad'
PRIOR='dc9662508a69659ad1fd1fead96ffda65978f63e5bdb4647bb742782d927f2b7'
PRODUCER='9194548d1b1c897d299d6f42b9336aa445a48f39c21f099a9b806b77fbace8fa'
FILES=['src/freedreno/ir3/ir3.h','src/freedreno/ir3/ir3_postsched.c','src/freedreno/ir3/ir3_sched.c']
UUIDS=('layout_UUID','device_UUID','driver_UUID')
def require(v,s):
 if not v:raise ValueError(s)
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def module(p,name):
 s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def source_contract(old,new,a,b):
 require(old['complete']and new['complete']and old['source_reverified']and new['source_reverified'],'unverified driver source')
 require(old['driver']['sha256']==OLD and new['driver']['sha256']==NEW,'unexpected driver identity')
 require(len(a)==len(b)==18218 and [k for k in sorted(a.keys()|b.keys())if a.get(k)!=b.get(k)]==FILES,'source changes outside exact compiler scheduling contract')
 require(old['tools']==new['tools']and old['declared_environment']==new['declared_environment'],'compiler environment changed')
def transition(root):
 A=root/'.local/build/turnip-internal-queue-sync-v1';B=root/'.local/build/turnip-memory-critical-schedule-v3';require(sha(A/'build.json')==OLD_BUILD and sha(B/'build.json')==NEW_BUILD,'frozen driver build digest');old=json.load(open(A/'build.json'));new=json.load(open(B/'build.json'));a=json.load(open(A/'candidate-source-ledger.json'));b=json.load(open(B/'candidate-source-ledger.json'));source_contract(old,new,a,b)
 for p,d,expected in [(A,old,OLD),(B,new,NEW)]:
  require(sha(d['driver']['path'])==expected,'actual ELF hash');require(sha(p/'candidate-source-ledger.json')==d['candidate_source_ledger_sha256'],'source ledger hash')
 helper=root/'tools/experiments/turnip-native-bc3/build.py';require(sha(helper)=='3fb441cd0db7cf4072461a4e445106ad505c7197db2877accff9f06068818491','original source verifier fingerprint');m=module(helper,'original_driver_source_verifier')
 require(m.manifest(A/'mesa-26.2.5')==a and m.manifest(B/'mesa-26.2.5')==b,'complete source bytes changed')
 require((B/'android-aarch64.ini').read_text()==(A/'android-aarch64.ini').read_text().replace("c_args = ['-fPIC']","c_args = ['-fPIC', '-DDM_IR3_MEMORY_CRITICAL_SCHED=1']"),'unexpected cross configuration')
 x=[v.replace(str(A),'<ROOT>')for v in old['setup_argv']];y=[v.replace(str(B),'<ROOT>')for v in new['setup_argv']];require(y==x+['-Dtu-build-id='+new['tu_build_id_hex']],'unexpected Mesa options')
 return {'old_build_sha256':OLD_BUILD,'new_build_sha256':NEW_BUILD,'members':len(a),'only_source_delta':FILES,'source_ledgers_reexecuted':True,'all_image_FDL_import_coherency_WSI_sources_unchanged':True,'separate_pipeline_cache_identity':True}
def query_from_compile(root,capture,capture_sha):
 require(sha(capture/'result.json')==capture_sha,'explicit actual capture digest mismatch');r=json.load(open(capture/'result.json'));require(r.get('actual2_pipeline_creations_completed')and r.get('fixture_stopped')and r.get('fixture_uninstalled')and r.get('main_unchanged')and not r.get('cleanup_errors'),'actual probe incomplete')
 bpath=root/'.local/build/fs-critical-schedule-v3-compile-v1/build.json';require(sha(bpath)==PROBE_BUILD and r['build_record_sha256']==PROBE_BUILD,'frozen original probe build');b=json.load(open(bpath));require(r['APK']==b['APK']and b['APK']['sha256']==APK and sha(b['APK']['path'])==APK,'actual probe APK');require(r['driver']['sha256']==NEW and r['actual_private_SHA']['files/libvulkan_freedreno.so']==NEW and r['ordinary_UID']>=10000,'actual private provider/UID')
 for name,h in b['source_inputs'].items():require(sha(name)==h,'probe source changed: '+name)
 require(sha(capture/'raw-ir3.log')==r['raw_IR3_sha256'],'actual compiler output hash')
 for name,h in b['payloads'].items():require(r['actual_private_SHA']['files/'+name]==h['sha256'],'actual original shader payload')
 pkg=b['package'];maps=(capture/'maps.txt').read_text();require('/'+pkg+'/files/libvulkan_freedreno.so'in maps,'private driver map missing');log=(capture/'raw-compile.log').read_text();require('FSCRITICALCOMPILE_BEGIN pid='+str(r['owned_PID'])+' uid='+str(r['ordinary_UID'])in log and'FSCRITICALCOMPILE_TERMINAL pipelines=2 queue_submits=0 game_modules_executed=0 no_timing=1'in log,'actual cold property query scope')
 patterns={'layout_UUID':r'LAYOUT_UUID advertised=1 value=([a-f0-9]{32})','device_UUID':r'deviceUUID=([a-f0-9]{32})','driver_UUID':r'driverUUID=([a-f0-9]{32})','pipeline_UUID':r'PIPELINE_UUID value=([a-f0-9]{32})'};q={}
 for k,p in patterns.items():
  values=re.findall(p,log);require(len(values)==1 and any(bytes.fromhex(values[0])),'actual single nonzero '+k);q[k]=values[0]
 return q,{'schema':'authenticated compiler-only standard Vulkan properties, not a producer bake','capture_path':str(capture),'capture_sha256':capture_sha,'APK_sha256':APK,'probe_build_sha256':PROBE_BUILD,'ordinary_uid':r['ordinary_UID'],'pid':r['owned_PID'],'query_log_sha256':sha(capture/'raw-compile.log'),'private_maps_sha256':sha(capture/'maps.txt'),'game_modules_executed':False,'GPU_commands_submitted':False}
def matching_uuids(old,new):
 for k in UUIDS:require(old[k]==new[k]and len(new[k])==32 and any(bytes.fromhex(new[k])),'layout compatibility mismatch: '+k)
def parser_cases(base,out,table,old,identity,pack):
 binary,record=base.compile_parser(out);refs={}
 for name,data in [('new',identity+bytes.fromhex(NEW)+bytes.fromhex(PRODUCER)),('old',identity+bytes.fromhex(OLD)+bytes.fromhex(PRODUCER)),('wrong',bytes([identity[0]^1])+identity[1:]+bytes.fromhex(NEW)+bytes.fromhex(PRODUCER)),('pack',pack)]:
  p=out/(name+'.identity');p.write_bytes(data);refs[name]=p
 bad=bytearray(table.read_bytes());bad[-1]^=1;corrupt=out/'negative-corrupt-table.bin';corrupt.write_bytes(bad);results={}
 for name,p,who,want in [('new_accepts',table,'new',1),('old_rejects_new',table,'old',0),('new_rejects_old',old,'new',0),('wrong_UUID_rejected',table,'wrong',0),('bad_digest_rejected',corrupt,'new',0)]:
  cmd=[str(binary),str(p),str(refs[who]),str(refs['pack']),str(want)];v=subprocess.run(cmd,check=True,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));results[name]=json.loads(v.stdout)
 corrupt.unlink();record['cases']=results;return record
def run(root,capture,capture_sha,out):
 require(not out.exists(),'new output required');q,auth=query_from_compile(root,capture,capture_sha);lineage=transition(root);prior_path=root/'.local/build/native-bc3-linear-consumer-12b8-v1/compatibility.json';require(sha(prior_path)==PRIOR,'authoritative original12b compatibility changed');prior=json.load(open(prior_path));require(prior['accepted']and prior['consumer_driver_sha256']==OLD and prior['producer_source_sha256']==PRODUCER,'original provenance mismatch');matching_uuids(prior['actual_UUIDs'],q)
 for n,h in prior['tool_inputs'].items():require(sha(root/n)==h,'original compatibility tool changed')
 base=module(root/'tools/experiments/native-layout-cache-compat/rebind.py','unchanged_original_table_tool');old=base.pin(prior['new_table']);manifest_path=base.pin(prior['original_provenance']['manifest']);manifest=json.load(open(manifest_path));source=manifest_path.parent/'native-layouts.bin';require(sha(source)==manifest['table_sha256'],'original0e table changed');data=source.read_bytes();require(data[160:]==old.read_bytes()[160:]and data[32:64].hex()==prior['original_pack_content_sha256'],'original recipe/family/pack mismatch')
 new=base.rewrite_table(data,manifest,NEW);out.mkdir(parents=True);table=out/'consumer-native-layouts.bin';table.write_bytes(new);identity=bytes.fromhex(''.join(q[k]for k in UUIDS));parser=parser_cases(base,out,table,old,identity,data[32:64]);require(new[160:]==data[160:]and new[:64]==data[:64]and new[96:128]==data[96:128],'payload/body identity changed')
 result={'schema':1,'contract':'DM exact scheduling-only compiler consumer host-layout compatibility v1','accepted':True,'consumer_table_generated':True,'rebaked':False,'source_bake_driver_sha256':prior['source_bake_driver_sha256'],'previous_consumer_driver_sha256':OLD,'consumer_driver_sha256':NEW,'original_pack_content_sha256':prior['original_pack_content_sha256'],'producer_source_sha256':PRODUCER,'actual_UUIDs':{k:q[k]for k in UUIDS},'actual_pipeline_UUID':q['pipeline_UUID'],'consumer_query_authentication':auth,'source_transition':lineage,'original_compatibility':{'path':str(prior_path),'sha256':PRIOR},'original_pack_provenance_preserved':True,'original_payloads_not_reexported_or_rehashed_in_this_step':True,'new_table':{'path':str(table),'sha256':sha(table)},'table_entry_family_body_unchanged':True,'production_parser':parser,'game_or_native_NVN_image_coherence_executed':False,'pending_runtime_gates':['exact runtime provider SHA plus original live import/coherence probes','actual native table/producer/pack checks without parser override','same controlled game geometry/visual output and wholeprofile performance'],'tool_inputs':{str(p):sha(p)for p in [Path(__file__).resolve(),root/'tools/experiments/native-layout-cache-compat/rebind.py',root/'tools/experiments/turnip-native-bc3/build.py']}};(out/'compatibility.json').write_text(json.dumps(result,indent=2)+'\n');return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[3]);p.add_argument('--capture',type=Path,required=True);p.add_argument('--capture-receipt-sha256',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=run(a.repo.resolve(),a.capture.resolve(),a.capture_receipt_sha256,a.output.resolve());print(json.dumps({'accepted':r['accepted'],'table':r['new_table'],'rebaked':False}))
if __name__=='__main__':main()
