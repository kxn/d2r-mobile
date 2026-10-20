#!/usr/bin/env python3
"""Offline guards using stdlib tar + prior original one-resource files, no GPU."""
import argparse
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
from native_batch_outputs import extract_outputs,output_limits,sha,validate_outputs
import native_batch_installer as installer


def test(a):
    cases=[];failures=0
    metadata=json.loads((a.baked/'native-resource.json').read_text())
    producer=metadata['producer_source_sha256']
    identity={k:metadata[k] for k in ('layout_UUID','device_UUID','driver_UUID')}
    entry={'id':'1'*64,'input':{'sha256':metadata['input_sha256']},'source':{'sha256':metadata['declaration_sha256']}}
    receipt={'ordinal':0,'entry_id':entry['id'],'complete':True,'input_sha256':entry['input']['sha256'],
             'declaration_sha256':entry['source']['sha256'],'producer_source_sha256':producer,
             'physical_image_bytes':metadata['physical_image_bytes'],'alignment':metadata['alignment'],
             'format':metadata['recipe']['VkFormat'],'width':metadata['recipe']['width'],
             'height':metadata['recipe']['height'],'levels':metadata['recipe']['mipLevels'],
             'original_driver_roundtrip_compressed_blocks_exact':True,**identity}
    leaves={}
    for leaf,field in [('native-resource.bin','native_payload_sha256'),('native-resource.json','native_metadata_sha256'),('native-resource-layout.bin','layout_record_sha256')]:
        data=(a.baked/leaf).read_bytes();leaves['items/'+entry['id']+'/'+leaf]=data;receipt[field]=hashlib.sha256(data).hexdigest()
    manifest_sha='a'*64
    complete={'schema':1,'complete':True,'entries':1,'physical_image_bytes':metadata['physical_image_bytes'],
              'producer_source_sha256':producer,'manifest_sha256':manifest_sha,'GPU_sampling_executed':False,'game_modules_executed':False}
    leaves['resource-batch-complete.json']=(json.dumps(complete)+'\n').encode()
    leaves['resource-batch-receipts.jsonl']=(json.dumps(receipt)+'\n').encode()
    with tempfile.TemporaryDirectory(prefix='native-batch-tar-guards-') as tmp:
        root=Path(tmp);counter=0
        def archive(records):
            nonlocal counter
            path=root/f'archive-{counter}.tar';counter+=1
            with tarfile.open(path,'w',format=tarfile.PAX_FORMAT) as tar:
                for info,data in records:tar.addfile(info,io.BytesIO(data) if info.isreg() else None)
            return path
        def records(values):
            out=[]
            for name,data in values.items():
                info=tarfile.TarInfo(name);info.size=len(data);out.append((info,data))
            return out
        good=archive(records(leaves));dest=root/'accepted'
        hashes=extract_outputs(good,dest,[entry]);assert set(hashes)==set(leaves)
        assert validate_outputs(dest,[entry],manifest_sha,identity,producer)==complete
        cases.append('original-one-resource-files-exact-tar-and-container-receipts-accepted-not-newGPU')
        def rejected(rows):
            nonlocal failures
            path=archive(rows);out=root/f'rejected-{counter}'
            try:extract_outputs(path,out,[entry]);raise RuntimeError('unsafe archive accepted')
            except (AssertionError,tarfile.TarError):failures+=1
            assert not out.exists(),'All headers must be checked before any extraction'
        rows=records(leaves)
        rejected(rows[:-1]);rejected(rows+rows[:1]);extra=tarfile.TarInfo('unexpected');extra.size=1;rejected(rows+[(extra,b'x')])
        for name in ('../outside','/absolute','./resource-batch-complete.json','items/../../outside'):
            altered=records(leaves);altered[0][0].name=name;rejected(altered)
        for kind in (tarfile.SYMTYPE,tarfile.LNKTYPE,tarfile.DIRTYPE):
            altered=records(leaves);altered[0][0].type=kind;altered[0][0].size=0;altered[0][0].linkname='../../outside';rejected(altered)
        altered=records(leaves);altered[0][0].pax_headers={'untrusted':'extended'};rejected(altered)
        altered=copy.deepcopy(leaves);altered['items/'+entry['id']+'/native-resource-layout.bin']=b'x'*991;rejected(records(altered))
        altered=copy.deepcopy(leaves);altered['items/'+entry['id']+'/native-resource.json']=b'x'*(65536+1);rejected(records(altered))
        cases.append('tar-exact-whitelist-duplicate-path-link-directory-PAX-size-reject-before-write')
        # Receipt failure never authorizes later batches; each uses untouched original files.
        for change in ('producer','manifest','UUID','input','payload','metadata-hash','layout-hash','alignment','entry-id','ordinal','falsecomplete'):
            d=root/('receipt-'+change);extract_outputs(good,d,[entry]);kwargs={'producer':producer,'manifest':manifest_sha,'identity':identity}
            r=copy.deepcopy(receipt)
            if change=='producer':kwargs['producer']='f'*64
            elif change=='manifest':kwargs['manifest']='f'*64
            elif change=='UUID':kwargs['identity']={**identity,'layout_UUID':'f'*32}
            elif change=='input':r['input_sha256']='f'*64
            elif change=='payload':
                path=d/'items'/entry['id']/'native-resource.bin';data=bytearray(path.read_bytes());data[-1]^=1;path.write_bytes(data)
            elif change=='metadata-hash':r['native_metadata_sha256']='f'*64
            elif change=='layout-hash':r['layout_record_sha256']='f'*64
            elif change=='alignment':r['alignment']+=4096
            elif change=='entry-id':r['entry_id']='2'*64
            elif change=='ordinal':r['ordinal']=1
            elif change=='falsecomplete':r['complete']=False
            (d/'resource-batch-receipts.jsonl').write_text(json.dumps(r)+'\n')
            try:validate_outputs(d,[entry],kwargs['manifest'],kwargs['identity'],kwargs['producer']);raise RuntimeError('bad receipt accepted '+change)
            except AssertionError:failures+=1
        cases.append('original-file-hashes-source-UUID-producer-size-alignment-ordinal-complete-mismatch-reject')
    xml='''<hierarchy><node package="com.android.packageinstaller" text="D2R Mobile"/><node package="com.android.packageinstaller" resource-id="com.android.packageinstaller:id/deleted_file_state_cb" text="已了解应用的风险检测结果" enabled="true" checked="false" bounds="[1,2][11,12]"/><node package="com.android.packageinstaller" resource-id="android:id/button1" text="继续安装" enabled="false" bounds="[1,20][11,30]"/></hierarchy>'''
    assert [x['kind'] for x in installer.actions(xml)]==['risk-ack']
    fresh=xml.replace('checked="false"','checked="true"').replace('enabled="false"','enabled="true"')
    assert [x['kind'] for x in installer.actions(fresh)]==['continue-install']
    assert installer.actions(xml.replace('D2R Mobile','Different App'))==[]
    assert installer.actions('<bad')==[]
    assert installer.actions(fresh.replace('com.android.packageinstaller','other.pkg'))==[]
    cases.append('exact-e391-installer-fresh-checkbox-before-continue-and-other-app-reject')
    report={'passed':True,'cases':cases,'negative_cases':failures,'scope':'Actual stdlib tar guards/receipt helper plus accepted installer actions. Container receipts generated around PRIOR original real one-resource files; NOT new producer/GPU/batch execution proof.','source':{'path':str(Path(__file__).with_name('native_batch_outputs.py')),'sha256':sha(Path(__file__).with_name('native_batch_outputs.py'))},'prior_metadata_sha256':sha(a.baked/'native-resource.json')}
    if a.report:a.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--baked',type=Path,required=True);p.add_argument('--report',type=Path)
    test(p.parse_args())
