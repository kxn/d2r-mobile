#!/usr/bin/env python3
"""Offline validation controls. Synthetic FS events are explicitly not device proof."""
from pathlib import Path
import argparse,copy,importlib.util,json,tempfile
HERE=Path(__file__).resolve().parent;R=Path('/home/dl/d2r-mobile')
spec=importlib.util.spec_from_file_location('identity_offline',HERE/'identity.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
D=R/'.local/logs/native-one-game-bb4ac54-q-actual-v2/native16';raw=json.loads((D/'native-binding-stops.json').read_text());native=(D/'native.log').read_text();C=R/'.local/build/turnip-one-native-overlay-final-v2';table=C/'files/native-layouts.bin';meta=json.loads((C/'composition.json').read_text());checks=[]
def no(name,fn):
 try:fn()
 except (AssertionError,KeyError,ValueError,TypeError):checks.append(name);return
 raise AssertionError(name+' accepted')
def yes(name,condition):assert condition,name;checks.append(name)
no('real-v2-transfer-only-is-insufficient-for-v3',lambda:m.verify(table,meta,raw,native))
# A synthetic copied frame with FS stage is only a host validator positive control.
positive=copy.deepcopy(raw);fs=copy.deepcopy(raw['events'][1]);fs['arguments']['stages']=0x80;positive['events'].append(fs)
scalar=lambda n:{'unsigned':n}
positive['events'][1]['typed_B2T']={'parameters':{'src':scalar(1),'tex':scalar(2),'has_view':scalar(1)},'actual_copy':{**{k:scalar(v)for k,v in [('imageSubresource.mipLevel',0),('imageSubresource.baseArrayLayer',0),('imageSubresource.layerCount',1),('imageOffset.x',0),('imageOffset.y',0),('imageOffset.z',0),('imageExtent.width',256),('imageExtent.height',256),('imageExtent.depth',1)]}}}
p=m.verify(table,meta,positive,native);yes('synthetic-transfer-plus-FS-selector-positive',p['identity_proven']and p['transfer_and_fragment_proven'])
yes('stdout-JSON-absence-does-not-weaken-validation','"native_resource_identity":true'not in native)
no('missing-validator-success-LOG-rejected',lambda:m.verify(table,meta,positive,native.replace('[native resources] fixed table installed,','[removed]')))
for field in ['driver_sha256','native_pack_content_sha256','producer_source_sha256']:
 wrong=copy.deepcopy(meta);wrong[field]='0'*64;no('wrong-'+field+'-rejected',lambda:m.verify(table,wrong,positive,native))
wrong=copy.deepcopy(positive);wrong['events'][2]['caller_images'][0]['recipe_bytes']['layout_uuid']['hex']='0'*32;no('raw-layout-UUID-mismatch-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);wrong['events'][2]['caller_images'][0]['recipe_bytes']['payload_sha256']['hex']='0'*64;no('raw-payload-SHA-mismatch-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);wrong['events'][2]['caller_images'][0]['image']['tex.serial']['unsigned']+=1;no('FS-serial-mismatch-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);wrong['events'][1]['typed_B2T']['actual_copy']['imageSubresource.mipLevel']['unsigned']=9;no('B2T-mip-outside-recipe-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);wrong['events'][1]['typed_B2T']['actual_copy']['imageExtent.width']['unsigned']=257;no('B2T-ROI-outside-mip-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);del wrong['events'][1]['typed_B2T'];no('missing-B2T-view-ROI-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);wrong['events'][2]['PID']+=1;no('FS-PID-mismatch-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);wrong['events'][0]['requirements']['alignment']['unsigned']*=2;no('Builder-alignment-mismatch-rejected',lambda:m.verify(table,meta,wrong,native))
wrong=copy.deepcopy(positive);wrong['events'][2]['allocation']['storage.memory']['unsigned']=0;no('missing-actual-VkDeviceMemory-rejected',lambda:m.verify(table,meta,wrong,native))
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp)/'native-layouts.bin';b=bytearray(table.read_bytes());b[400]^=1;p.write_bytes(b);no('corrupted-table-rejected',lambda:m.verify(p,meta,positive,native))
s=(HERE/'transaction.py').read_text();yes('HUD-evidence-before-provider-and-native-postconditions',s.index("screenshot_collect(dev,own,d,path,output,'HUD-before-postconditions')")<s.index("result['requested_shared_guest_heap']"))
yes('finally-retries-owned-screenshot-pull-CAS-cleanup',s.index("for shot in d.get('screenshots',[]):")>s.index(' finally:'))
yes('original-fivepoint-route-prefix-only','route=[(5215,6289),(5225,6295),(5235,6289)]'in s)
arg=argparse.ArgumentParser();arg.add_argument('--report',type=Path,required=True);a=arg.parse_args();a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps({'offline':True,'ADB_access':False,'count':len(checks),'checks':checks,'boundary':'Only negative/positive host validator controls; synthetic FS copied from older TRANSFER stop is not actual fragment evidence. Real v3 acceptance requires independently owned official LLDB v2 actual FS.'},indent=2)+'\n');print(len(checks),'identity controls passed')
