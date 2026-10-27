#!/usr/bin/env python3
"""Verify opaque cache compatibility; write a new consumer table, never rebake/relabel payloads."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import shutil

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
OLD_DRIVER='0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
NEW_DRIVER='12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5'
OLD_BUILD_SHA='a71ee27bb8019cdec4d10a12fb14cadf76eef8c4e0681e9d8eec860cb2e085cc'
NEW_BUILD_SHA='424afe4f84f13a2303fc4dd6f0c6d20af65b2931d3697d55fef3e627a036aba1'
PACKING_SHA='c7a6896a175128d85d5a1ca7196714984293018dd6b0fa86eeeb3a51c0493796'
PACKER_SHA='e99e62fc3cafe2315b0a17857787801b060ae68b3bad6a0655490290d4d9e770'
POLICY='src/vulkan/util/vk_extensions.py'
UUIDS=('layout_UUID','device_UUID','driver_UUID')

def require(ok,message):
    if not ok: raise ValueError(message)
def digest(data): return hashlib.sha256(data).hexdigest()
def file_sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':')).encode()
def load_module(path,name):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def pin(record):
    p=Path(record['path']);require(file_sha(p)==record['sha256'],'input file digest mismatch: '+str(p));
    if 'bytes' in record:require(p.stat().st_size==record['bytes'],'input file size mismatch: '+str(p))
    return p

def source_transition(old_path,new_path):
    require(file_sha(old_path)==OLD_BUILD_SHA and file_sha(new_path)==NEW_BUILD_SHA,'frozen source/build record mismatch')
    old,new=json.loads(old_path.read_text()),json.loads(new_path.read_text())
    for r,sha in [(old,OLD_DRIVER),(new,NEW_DRIVER)]:
        require(r['complete'] and r['source_reverified'] and r['driver']['sha256']==sha,'incomplete or wrong driver build')
        require(file_sha(r['driver']['path'])==sha,'driver ELF digest mismatch')
    old_ledger_path=old_path.parent/'candidate-source-ledger.json';new_ledger_path=new_path.parent/'candidate-source-ledger.json'
    require(file_sha(old_ledger_path)==old['candidate_source_ledger_sha256'],'old source ledger digest')
    require(file_sha(new_ledger_path)==new['candidate_source_ledger_sha256'],'new source ledger digest')
    a,b=json.loads(old_ledger_path.read_text()),json.loads(new_ledger_path.read_text())
    require([k for k in sorted(a.keys()|b.keys()) if a.get(k)!=b.get(k)]==[POLICY],'source changes outside single approved Android gate')
    require(file_sha(ROOT/'tools/experiments/turnip-native-bc3/build.py')=='3fb441cd0db7cf4072461a4e445106ad505c7197db2877accff9f06068818491','baseline verifier source changed')
    h=load_module(ROOT/'tools/experiments/turnip-native-bc3/build.py','original_bc3_builder')
    for path,ledger in [(old_path,a),(new_path,b)]:require(h.manifest(path.parent/'mesa-26.2.5')==ledger,'full driver source ledger mismatch')
    require(old['setup_argv'][7:]==new['setup_argv'][7:] and old['declared_environment']==new['declared_environment'] and old['tools']==new['tools'],'driver build options/tools changed')
    generated_old=old_path.parent/'mesa-kgsl-build/src/vulkan/util/vk_extensions.c';generated_new=new_path.parent/'mesa-kgsl-build/src/vulkan/util/vk_extensions.c'
    before,after=generated_old.read_bytes(),generated_new.read_bytes();needle=b'   .KHR_internally_synchronized_queues = false,'
    require(before.count(needle)==1 and after==before.replace(needle,b'   .KHR_internally_synchronized_queues = ANDROID_API_LEVEL >= 34,'),'generated extension table differs beyond approved gate')
    return {'old_source_ledger_sha256':file_sha(old_ledger_path),'new_source_ledger_sha256':file_sha(new_ledger_path),'members':len(a),'only_source_change':POLICY,'generated_single_gate_verified':True}

def compare_queries(old,new):
    for q in [old,new]:
        require(q.get('contract')=='DM native image producer v1' and q.get('complete') is True and q.get('driverID')==18,'incomplete actual producer query')
        for k in UUIDS:
            v=q.get(k);require(isinstance(v,str) and len(v)==32 and v==v.lower() and any(bytes.fromhex(v)),'invalid actual '+k)
    require(all(old[k]==new[k] for k in UUIDS),'actual layout/device/driver UUID mismatch')
    require(old['producer_source_sha256']==new['producer_source_sha256'],'actual producer source mismatch')
    require((old['vendorID'],old['deviceID'])==(new['vendorID'],new['deviceID']),'actual physical GPU mismatch')
    return bytes.fromhex(''.join(old[k] for k in UUIDS)),bytes.fromhex(old['producer_source_sha256'])

def authenticate_query(query_path,receipt_path,new):
    require(receipt_path is not None and receipt_path.is_file(),'missing actual consumer query authentication receipt')
    receipt=json.loads(receipt_path.read_text())
    require(receipt.get('driver_sha256')==NEW_DRIVER and not receipt.get('cleanup_errors') and receipt.get('original_unchanged',True),'consumer query receipt driver/cleanup mismatch')
    matches=[]
    frozen_controller=ROOT/'.local/logs/native-bc3-three-arm-game-v1/run-internal-queue-producer-probe.py'
    actual_reference=(receipt.get('stage')=='actual-bc3-native-owned-probe-passed' and receipt.get('baseline_bound') is True and receipt.get('new_driver_build_sha256')==NEW_BUILD_SHA and receipt.get('controller_sha256')=='76f2fa967dd4ee8cc7d6913c5253892402fa90f8049a87a03cc1fdeb5bada511' and file_sha(frozen_controller)==receipt.get('controller_sha256'))
    map_evidence=[]
    for run in receipt.get('runs',[]):
        identity=run.get('queried_identity',{})
        if not all(identity.get(k)==new[k] for k in UUIDS):continue
        aliases=run.get('driver_alias_stat',{})
        candidates=[(p,v) for p,v in aliases.items() if v.get('sha256')==NEW_DRIVER]
        maps=run.get('mapped_driver',[])
        if any(any(p in line and str(v['inode']) in line for line in maps) for p,v in candidates):matches.append(run)
        elif actual_reference and run.get('result')=='passed' and run.get('baked_outputs',{}).get('native-resource.json')==file_sha(query_path):
            # This frozen controller asserts the actual app-private driver SHA
            # immediately after reading maps, and pins every pulled output hash.
            map_path=receipt_path.parent/(run['name']+'-maps.txt')
            lines=map_path.read_text().splitlines()
            require(any('files/runtime/vulkan-driver/libvulkan_freedreno.so' in line for line in lines),'authenticated query lacks actual custom-driver map')
            config=[row for row in run['rows'] if row.get('native_image_app_configuration')]
            require(len(config)==1 and config[0]['uid']>=10000 and config[0]['game_modules_executed'] is False,'query not isolated ordinary UID')
            require(run.get('actual_baked_record')==new,'actual pulled query record differs')
            matches.append(run);map_evidence.append({'path':str(map_path),'sha256':file_sha(map_path),'ordinary_uid':config[0]['uid'],'controller_sha256':receipt['controller_sha256']})
    require(matches,'actual consumer query lacks matching mapped driver inode/SHA and queried identity')
    # Query bytes must have a recorded pull hash, rather than trusting just a user supplied JSON.
    query_sha=file_sha(query_path)
    def values(v):
        if isinstance(v,dict):
            for x in v.values():yield from values(x)
        elif isinstance(v,list):
            for x in v:yield from values(x)
        else:yield v
    require(query_sha in set(x for x in values(receipt) if isinstance(x,str)),'actual query JSON hash absent from authenticated receipt')
    return {'path':str(receipt_path.resolve()),'sha256':file_sha(receipt_path),'query_sha256':query_sha,'matching_runs':len(matches),'map_evidence':map_evidence}

def rewrite_table(table,manifest,new_driver):
    require(len(table)>=160 and table[:8]==b'DMNTBL2\0','unsupported original native table')
    ver,hdr,count,entry,size=struct.unpack_from('<4IQ',table,8)
    require((ver,hdr,count,entry,size)==(2,160,6217,1040,len(table)) and len(table)==160+count*1056,'original table count/shape mismatch')
    require(digest(table)==manifest['table_sha256'] and table[64:96].hex()==OLD_DRIVER,'original table/driver mismatch')
    sealed=bytearray(table);expected=bytes(sealed[128:160]);sealed[128:160]=bytes(32);require(hashlib.sha256(sealed).digest()==expected,'original table digest mismatch')
    require(table[32:64].hex()==manifest['pack_content_sha256'] and table[96:128].hex()==manifest['producer_source_sha256'],'original table pack/producer mismatch')
    sealed[64:96]=bytes.fromhex(new_driver);sealed[128:160]=hashlib.sha256(sealed).digest()
    require(sealed[:64]==table[:64] and sealed[96:128]==table[96:128] and sealed[160:]==table[160:],'non-consumer table bytes changed')
    return bytes(sealed)

def compile_parser(output):
    cc=shutil.which('cc');require(cc,'host C compiler unavailable');binary=output/'production-table-parser'
    sources=[HERE/'parser.c',ROOT/'dm/gfx/nvn/nvn_native_layout.c',ROOT/'dm/gfx/nvn/nvn_tex.c',ROOT/'dm/third_party/runtime-codecs/sha256.c']
    cmd=[cc,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DMBEDTLS_CONFIG_FILE="dm_sha_config.h"','-I'+str(ROOT/'dm/gfx/nvn'),'-I'+str(ROOT/'dm/third_party/runtime-codecs'),*map(str,sources),'-o',str(binary)]
    subprocess.run(cmd,check=True,capture_output=True,text=True)
    return binary,{'command':cmd,'compiler_sha256':file_sha(Path(cc).resolve()),'source_sha256':{str(p):file_sha(p) for p in sources},'binary_sha256':file_sha(binary)}

def parser_cases(output,table,oldtable,identity,producer,pack):
    binary,record=compile_parser(output);paths={}
    for name,value in [('consumer',identity+bytes.fromhex(NEW_DRIVER)+producer),('original',identity+bytes.fromhex(OLD_DRIVER)+producer),('wrong_uuid',bytes([identity[0]^1])+identity[1:]+bytes.fromhex(NEW_DRIVER)+producer),('pack',pack)]:
        paths[name]=output/(name+'.identity');paths[name].write_bytes(value)
    bad=bytearray(table.read_bytes());bad[-1]^=1;(output/'corrupt-table-host-test.bin').write_bytes(bad)
    cases=[('new_accepts',table,'consumer',1),('old_rejects_new',table,'original',0),('new_rejects_old',oldtable,'consumer',0),('wrong_uuid_rejected',table,'wrong_uuid',0),('bad_digest_rejected',output/'corrupt-table-host-test.bin','consumer',0)]
    results={}
    for name,t,who,expected in cases:
        cmd=[str(binary),str(t),str(paths[who]),str(paths['pack']),str(expected)]
        run=subprocess.run(cmd,check=True,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));results[name]=json.loads(run.stdout)
    (output/'corrupt-table-host-test.bin').unlink();record['cases']=results;return record

def verify_pack(packing_path,old_build,query_path):
    require(file_sha(packing_path)==PACKING_SHA,'original packing provenance digest mismatch')
    packing=json.loads(packing_path.read_text());manifest_path=pin(packing['native_pack']);manifest=json.loads(manifest_path.read_text());root=manifest_path.parent
    for key in ['bulk_result','bulk_receipt_index','prepared','original_cache_file','driver_build','actual_driver_query']:pin(packing[key])
    require(Path(packing['driver_build']['path']).resolve()==old_build.resolve() and Path(packing['actual_driver_query']['path']).resolve()==query_path.resolve(),'original build/query provenance path mismatch')
    require(manifest['complete'] and len(manifest['entries'])==6217 and manifest['driver_binary_sha256']==OLD_DRIVER,'original pack incomplete or wrong source driver')
    require(file_sha(ROOT/'scripts/dev/pack-native-texture.py')==PACKER_SHA,'original packer changed')
    packer=load_module(ROOT/'scripts/dev/pack-native-texture.py','original_native_packer')
    old_identity,old_producer=packer.queried_identity(query_path)
    require(old_identity.hex()==manifest['identity'] and old_producer.hex()==manifest['producer_source_sha256'],'original pack/query identity mismatch')
    old_table=root/'native-layouts.bin';table=old_table.read_bytes();rewrite_table(table,manifest,NEW_DRIVER)
    index=json.loads(Path(packing['bulk_receipt_index']['path']).read_text());prepared=json.loads(Path(packing['prepared']['path']).read_text());by_logical={e['logical'].lower():e for e in prepared['entries']};outputs={};verified={}
    for ref in index['results']:
        result_path=pin(ref);result=json.loads(result_path.read_text());require(result['driver_sha256']==OLD_DRIVER and result['producer_source_sha256']==manifest['producer_source_sha256'] and not result['cleanup_errors'] and result['original_unchanged'],'original bake batch provenance mismatch')
        for name,sha in result['baked_outputs'].items():
            p=result_path.parent/name;require(file_sha(p)==sha,'original baked output hash mismatch: '+str(p));verified[str(p.resolve())]=sha
        for ident in prepared['batches'][result['batch_index']]['entry_ids']:
            require(ident not in outputs,'duplicate source bake item');outputs[ident]=result_path.parent/'items'/ident
    entry_receipts=[]
    for n,detail in enumerate(manifest['entries'],1):
        original=by_logical[detail['logical'].lower()]
        for key in ['source','input','prepare_report','texture']:pin(original[key])
        spec={'source':original['source']['path'],'texture':original['texture']['path'],'baked':str(outputs[original['id']])}
        entry,texture,desc,rebuilt,identity=packer.load_entry(spec,n)
        require(rebuilt==detail and identity==old_identity,'original full entry identity/content differs: '+detail['logical'])
        require(entry==table[160+(n-1)*1040:160+n*1040],'original table recipe/body differs')
        family_offset=160+6217*1040+(n-1)*16
        require(table[family_offset:family_offset+16]==struct.pack('<QII',detail['original_layout_data'],detail['thumbnail_mip'] or 0,0),'original table source family bytes differ')
        p=root/detail['logical'];require(file_sha(p)==detail['texture_sha256'] and digest(texture)==detail['texture_sha256'],'pack texture bytes mismatch')
        descriptor=p.with_suffix('.descriptor.json');require(digest(canonical(json.loads(descriptor.read_text())))==detail['descriptor_sha256'],'pack descriptor mismatch')
        entry_receipts.append({'id':n,'logical':detail['logical'],'bake_driver_sha256':OLD_DRIVER,'original_native_recipe_sha256':detail['native_recipe_sha256'],'original_payload_sha256':detail['native_payload_sha256'],'original_texture_sha256':detail['texture_sha256'],'packed_descriptor_file_sha256':file_sha(descriptor)})
    require(len(outputs)==len(by_logical)==6217,'not the complete original selection')
    cache=root/'data/hd/texture_desc_cache.json';require(file_sha(cache)==manifest['complete_cache_merge']['output_sha256'],'complete descriptor cache changed')
    require(file_sha(root/'replacements.tsv')==manifest['replacements_sha256'],'original replacement list changed')
    require(hashlib.sha256(canonical({'entries':manifest['entries'],'driver_sha256':OLD_DRIVER,'producer_source_sha256':manifest['producer_source_sha256']})).hexdigest()==manifest['pack_content_sha256'],'original pack content identity mismatch')
    return manifest,old_table,table,{'packing':{'path':str(packing_path),'sha256':file_sha(packing_path)},'manifest':{'path':str(manifest_path),'sha256':file_sha(manifest_path)},'actual_bake_files_verified':len(verified),'full_selected_entries':6217,'entry_provenance':entry_receipts,'all_pack_texture_and_descriptor_bytes_verified':True,'complete_descriptor_cache_sha256':file_sha(cache)}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--consumer-query',type=Path);p.add_argument('--consumer-query-receipt',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--pending',action='store_true');a=p.parse_args()
    require(not a.output.exists(),'output must be new; preserve prior results')
    if not a.consumer_query or not a.consumer_query.is_file():
        if a.pending:
            a.output.mkdir(parents=True);(a.output/'pending.json').write_text(json.dumps({'accepted':False,'reason':'actual consumer driver query missing','consumer_driver_sha256':NEW_DRIVER,'consumer_table_generated':False},indent=2)+'\n');return
        raise ValueError('missing actual consumer driver query; no accepted table may be generated')
    old_build=ROOT/'.local/build/turnip-native-bc3-v1/build.json';new_build=ROOT/'.local/build/turnip-internal-queue-sync-v1/build.json'
    transition=source_transition(old_build,new_build)
    packing_path=ROOT/'.local/build/native-original-bc3-linear-pack-v2/packing.json';packing=json.loads(packing_path.read_text());old_query_path=pin(packing['actual_driver_query'])
    old_query,new_query=json.loads(old_query_path.read_text()),json.loads(a.consumer_query.read_text());identity,producer=compare_queries(old_query,new_query);authenticated=authenticate_query(a.consumer_query,a.consumer_query_receipt,new_query)
    manifest,old_table,table,provenance=verify_pack(packing_path,old_build,old_query_path)
    a.output.mkdir(parents=True);new_table=a.output/'consumer-native-layouts.bin';new_table.write_bytes(rewrite_table(table,manifest,NEW_DRIVER))
    parser=parser_cases(a.output,new_table,old_table,identity,producer,table[32:64])
    receipt={'schema':1,'contract':'DM explicit host-layout cache compatibility v1','accepted':True,'consumer_table_generated':True,'rebaked':False,'game_or_GPU_executed':False,'source_bake_driver_sha256':OLD_DRIVER,'consumer_driver_sha256':NEW_DRIVER,'original_pack_content_sha256':manifest['pack_content_sha256'],'producer_source_sha256':producer.hex(),'actual_UUIDs':{k:new_query[k] for k in UUIDS},'consumer_query_authentication':authenticated,'source_transition':transition,'original_provenance':provenance,'new_table':{'path':str(new_table.resolve()),'sha256':file_sha(new_table)},'table_entry_family_body_unchanged':digest(table[160:])==digest(new_table.read_bytes()[160:]),'original_pack_rewritten':False,'production_parser':parser,'standard_contract':'VK_EXT_host_image_copy optimalTilingLayoutUUID compatible cached opaque MEMCPY layout, further restricted to exact same device/driver UUID/producer and sole source gate delta','tool_inputs':{str(f.relative_to(ROOT)):file_sha(f) for f in [Path(__file__).resolve(),HERE/'parser.c']},'pending_runtime_gates':['actual new-driver native-NVN functional compatibility','game candidate acceptance and measured performance']}
    (a.output/'compatibility.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n');print(json.dumps({'accepted':True,'entries':6217,'consumer_table':receipt['new_table'],'rebaked':False}))
if __name__=='__main__':main()
