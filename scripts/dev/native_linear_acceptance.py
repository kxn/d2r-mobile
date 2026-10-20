"""Read only actual f830 original-NVN and filter receipts; no game acceptance."""
import json
from pathlib import Path
ROOT=Path('/home/dl/d2r-mobile')
F830='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
AA='aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db'
ROOT_SHA='9089fbe5d64b3842e8ae387506e6a22c9cc2034881a76e2711a675407f05af0f'
FILTER_SHA='02eff2027890cf9aff0ad479def9ae780e0fa966ec9f3a6a474824855c4f6261'
def check(value,message):
 if not value:raise ValueError(message)
def acceptance(path,expected,hashes):
 check(expected==ROOT_SHA and hashes.file(path)==ROOT_SHA,'exact current root original-NVN review required')
 r=json.loads(path.read_text());check(r['status']=='ROOT_REVIEWED_F830_ACTUAL_ORIGINAL_NVN_PASS_AND_OLD_AA_TABLE_REFUSAL' and r['ordinary_UID_only'] and r['original_game_unchanged'] and r['cleanup_complete'],'original-NVN review incomplete')
 results={}
 for key,stage in [('positive','actual-original-NVN-native-window-family-owned-passed'),('negative','actual-old-AA-native-table-refused-by-original-runtime')]:
  p=hashes.record(r[key]);d=json.loads(p.read_text());check(d['stage']==stage and not d['cleanup_errors'] and d['original_unchanged'] and all(d[k]for k in ['fixture_PID_absent','fixture_uninstalled','fixture_tmp_absent','owned_clients_stopped']) and d['driver_sha256']==F830,'actual NVN fixture failed/unclean');results[key]=d
 actual=results['positive'];rows=actual['runs'][0]['rows'];check(actual['runs'][0]['first_pid']==r['positive_PID'] and all(row in rows for row in r['positive_rows']),'root review actual rows differ')
 identity=next(row for row in rows if row.get('native_resource_identity'))
 for name,key,count in [('native-resource-original-NVN','checks',447),('native-image-only-original-NVN','checks_added',36),('native-previous-writer-ASTC-pending-FS','checks',40)]:
  matching=[row for row in rows if row.get('test')==name];check(len(matching)==1 and matching[0]['result']=='passed' and matching[0][key]==count,'strict original-NVN oracle differs')
 fp=ROOT/'.local/logs/native-astc-linear-filter-reference-q-v1-root-actual-review.json';check(hashes.file(fp)==FILTER_SHA,'filter root review changed');f=json.loads(fp.read_text());check(f['status']=='ROOT_REVIEWED_F830_NATIVE_BILINEAR_TRILINEAR_GPU_REFERENCE_PASS' and f['cleanup_complete'] and f['original_unchanged'] and f['proof']['linear_trilinear_GPU_reference_floatbits_proven'],'filter review incomplete')
 d=json.loads(hashes.record(f['actual_result']).read_text());check(d['stage']=='actual-native-ASTC-fully-linear-filter-reference-passed-not-game' and not d['cleanup_errors'] and d['original_unchanged'] and d['driver_sha256']==F830,'actual filter result failed/unclean')
 check(all(f['proof']['identity'][k]==identity[k] for k in ['layout_UUID','device_UUID','driver_UUID']),'NVN/filter three UUIDs differ')
 # Explicitly retain actual finite fixture packaging provenance. This is the
 # original 447 check resource, not a claim that future 6217 entries ran.
 artifact=ROOT/'.local/build/native-astc-linear-NVN-positive-v2'
 manifest_path=artifact/'candidate.apk.build.json';check(hashes.file(manifest_path)=='f287cc773ed31a4fca6a5007f942c0184ba3c98e5715c5bfc70cef759126b13c','finite companion descriptor changed');manifest=json.loads(manifest_path.read_text())
 check(manifest['owned_resource_inputs']==actual['owned_resource_inputs'] and manifest['apk_sha256']==actual['APK_sha256'],'finite actual package/resource mismatch')
 for leaf,digest in actual['owned_resource_inputs'].items():check(hashes.file(artifact/'resource4'/leaf)==digest,'finite actual resource changed')
 return {'driver_sha256':F830,'bulk_assets_accepted':False,'production_promotion':False,
  'acceptance_scope':'actual-original-NVN-447-36-40-and-fractional-filter-not-game-or-bulk',
  'identity':{'producer_sha256':manifest['native_resource_producer_sha256'],
   'actual_recipe_UUID_payload_input_equal_table':{key:identity[name]for key,name in [('layout_uuid','layout_UUID'),('device_uuid','device_UUID'),('driver_uuid','driver_UUID')]}},
  'actual_game_accepted':False}
def historical_producer_driver(ready,receipt,hashes):
 """Compiled 8064 provenance remains AA; only current controller targets f830."""
 if receipt['driver_sha256']!=F830:
  return ready['canonical']['driver_sha256']==receipt['driver_sha256']
 check(receipt['driver_sha256']==F830 and ready['canonical']['driver_sha256']==AA,'unreviewed historical/current driver split')
 hashes.record(ready['ordinary_UID_controller'])
 active=ready['active_driver'];check(active['sha256']==F830,'active producer target differs');hashes.record(active)
 frozen=json.loads(hashes.record(ready['active_driver_ready']).read_text());check(frozen['driver']['sha256']==F830,'active frozen driver record differs')
 old=ready['historical_compiled_producer_ready'];check(old['sha256']=='1f88055509ac41148b4d6cea1c97b48a119d929c67511be7c401d00ea5b2066c','historical compiled producer source record differs')
 predecessor=json.loads(hashes.record(old).read_text());check(predecessor['canonical']==ready['canonical'] and predecessor['producer_source_identity']==ready['producer_source_identity'] and predecessor['producer_source_sha256']==ready['producer_source_sha256']=='8064b80022370de2832964ee22015539441981e7272270b0ff01f5b6c6062ca3','compiled producer identity altered')
 return True
