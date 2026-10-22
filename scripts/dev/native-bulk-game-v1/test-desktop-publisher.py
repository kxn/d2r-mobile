"""Host filesystem transaction controls, never a delivery receipt/device proof.
Reuse actual accepted Device.identity/digest/stopped + inventory/rename functions
with only the ADB transport replaced by a temporary host directory. No game,
installer, emulator, sampling, or synthetic receipt reaches CLI --execute.
"""
from pathlib import Path
import hashlib, importlib.util, json, subprocess, tempfile, unittest
from unittest.mock import patch
D=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('desktop_publisher',D/'publish-qualified-desktop.py')
pub=importlib.util.module_from_spec(s);s.loader.exec_module(pub)
H=lambda b:hashlib.sha256(b).hexdigest()

class LocalDevice(pub.m.common.Device):
 def __init__(self,root):self.root=root;self.command=['NO_ADB','HOST'];self.live=False;self.install_calls=[]
 def translate(self,value):
  if value.startswith('/data/local/tmp/'):return str(self.root/'remote'/Path(value).name)
  return value
 def app(self,*args):
  return subprocess.check_output([self.translate(str(a))for a in args],cwd=self.root,text=True).strip()
 def adb(self,*args):
  if args==('shell','pidof '+pub.PKG+' || true'):return 'UNOWNED' if self.live else ''
  if args==('shell','pm','path',pub.PKG):return 'package:'+str(self.root/'installed.apk')
  if args[:2]==('shell','sha256sum'):return self.app(*args[1:])
  if args[:3]==('shell','getprop','ro.build.fingerprint'):return 'qualified/fingerprint'
  if args[:4]==('shell','settings','get','global'):return 'null'
  if args[0]=='push':
   Path(self.translate(args[2])).write_bytes(Path(args[1]).read_bytes());return ''
  if args[:3]==('shell','test','!'):return self.app(*args[1:])
  if args[0]=='shell' and len(args)==2:
   line=args[1]
   # Only localize the owned transfer path; execute the original shell CAS.
   for file in (self.root/'remote').iterdir():line=line.replace('/data/local/tmp/'+file.name,str(file))
   return subprocess.check_output(['sh','-c',line],cwd=self.root,text=True).strip()
  raise AssertionError(('unexpected device command',args))

class Publication(unittest.TestCase):
 def setUp(self):
  self.t=tempfile.TemporaryDirectory(prefix='desktop-publication-host-');self.addCleanup(self.t.cleanup)
  self.root=Path(self.t.name);self.dev=LocalDevice(self.root)
  (self.root/'remote').mkdir();runtime=self.root/'files/runtime';runtime.mkdir(parents=True)
  (runtime/'shaders').mkdir();(runtime/'shaders/.chain').write_text('9507875007c6\n')
  (runtime/'save').mkdir();(runtime/'save/hero').write_bytes(b'LATEST-SAVE')
  self.old_save=H(b'LATEST-SAVE');self.out=self.root/'report';self.out.mkdir()
  self.oldapk=self.root/'old.apk';self.oldapk.write_bytes(b'old APK');self.newapk=self.root/'new.apk';self.newapk.write_bytes(b'new APK')
  (self.root/'installed.apk').write_bytes(self.oldapk.read_bytes())
  self.raw=self.root/'raw';self.raw.mkdir();names=['rewrite-manifest.json']+['module-'+str(i)for i in range(27)]
  self.modules={}
  for n in names:(self.raw/n).write_bytes(('new '+n).encode());self.modules[n]=H((self.raw/n).read_bytes())
  self.overlay=self.root/'overlay';self.overlay.mkdir();(self.overlay/'ctrl_touch').write_bytes(b'new qualified overlay')
  self.driver=self.root/'driver.so';self.driver.write_bytes(b'qualified driver')
  self.table=self.root/'native-layouts.bin';self.table.write_bytes(b'qualified table')
  baseline={}
  for label,target in pub.r.DIRS:
   p=self.root/target;p.mkdir(parents=True)
   if label=='modules':
    for n in names:(p/n).write_bytes(('old '+n).encode())
   elif label=='temp':(p/'vulkan-pso').mkdir();(p/'vulkan-pso/known.bin').write_bytes(b'learned original PSO')
   else:(p/'original').write_bytes(('old '+label).encode())
   inv=pub.m.inventory(self.dev,target);baseline[target]={target+'/'+n:h for n,h in inv.items()}
  self.original_dirs={target:pub.m.inventory(self.dev,target)for _,target in pub.r.DIRS}
  for n in (pub.PROFILE,pub.RECEIPT):(self.root/n).write_bytes(('old '+n).encode())
  self.original_files={n:self.dev.digest(n)for n in (pub.PROFILE,pub.RECEIPT)}
  receipt=self.root/'receipt.json';receipt.write_text('{"synthetic_host_test_only":true}')
  phase={'kind':'native','overlay':{'ctrl_touch':H(b'new qualified overlay')},'overlay_root':str(self.overlay),
    'native_table':{'path':str(self.table),'sha256':H(self.table.read_bytes())}}
  oldmodules={n:{'sha256':H(('old '+n).encode())}for n in names}
  old={'apk':H(self.oldapk.read_bytes()),'chain':'9507875007c6','module_artifacts':oldmodules}
  new={'apk':H(self.newapk.read_bytes()),'chain':'9507875007c6','module_artifacts':{n:{'sha256':h}for n,h in self.modules.items()}}
  p={'serial':'HOST','old':old,'new':new,'new_apk':new['apk'],'new_elf':'test ELF','old_apk':str(self.oldapk),
    'apk':str(self.newapk),'driver_path':str(self.driver),'driver':{'sha256':H(self.driver.read_bytes())},
    'raw_bundle':str(self.raw),'modules':self.modules,'baseline_data':{'chain':'9507875007c6','inventories':baseline}}
  self.x={'p':p,'phase':phase,'outer':{'delivery_receipt':{'path':str(receipt),'sha256':H(receipt.read_bytes())}},
   'delivery':{'device_fingerprint':'qualified/fingerprint'},'profile_bytes':b'new tested profile\n',
   'original_shader_inventory':pub.m.inventory(self.dev,'files/runtime/shaders')}
 def install(self,dev,apk,expected,out,require,pkg):
  require(not dev.live,'installer must be stopped');require(H(apk.read_bytes())==expected,'installer input changed')
  dev.install_calls.append(expected);(self.root/'installed.apk').write_bytes(apk.read_bytes())
 def round(self,dev,x,out,n):
  self.assertEqual((self.root/pub.PROFILE).read_bytes(),x['profile_bytes'])
  self.assertEqual(dev.digest(pub.RECEIPT),x['outer']['delivery_receipt']['sha256'])
  out.mkdir();return {'ordinal':n,'no_Intent_extras':True,'lifecycle':{'synthetic_host_test_only':True}}
 def runpub(self,round=None,install=None):
  return pub.publish(self.x,self.out,self.dev,install=install or self.install,game_round=round or self.round)
 def assert_restored(self,d):
  self.assertTrue(d['original_restored']);self.assertFalse(d['cleanup_errors'])
  self.assertEqual(H((self.root/'installed.apk').read_bytes()),self.x['p']['old']['apk'])
  for target,inv in self.original_dirs.items():self.assertEqual(pub.m.inventory(self.dev,target),inv)
  for n,h in self.original_files.items():self.assertEqual(self.dev.digest(n),h)
 def test_success_retains_new_combination_and_backups_after_two_rounds(self):
  d=self.runpub();self.assertTrue(d['committed']);self.assertEqual(len(d['rounds']),2)
  self.assertFalse(d['cleanup_errors']);self.assertTrue(d['lock_released']);self.assertTrue(d['transfer_removed'])
  self.assertTrue((self.root/d['private_backup']/'backup/modules').is_dir())
  self.assertEqual(self.dev.digest(pub.PROFILE),H(self.x['profile_bytes']))
  self.assertEqual((self.root/'files/runtime/save/hero').read_bytes(),b'LATEST-SAVE')
  for _,target in pub.r.DIRS:self.assertNotIn(pub.m.OWNER,pub.m.inventory(self.dev,target))
 def test_actual_old_baseline_absent_native_and_profile_restored_as_absent(self):
  import shutil
  target='files/runtime/native-one-resources';shutil.rmtree(self.root/target)
  self.x['p']['baseline_data']['inventories'][target]=None;self.original_dirs[target]=None
  for n in (pub.PROFILE,pub.RECEIPT):(self.root/n).unlink();self.original_files[n]=None
  def fail(dev,x,out,n):raise RuntimeError('new desktop profile failed; old baseline has no profile/native table')
  self.assert_restored(self.runpub(round=fail))
 def test_preexisting_user_process_is_not_adopted_or_modified(self):
  self.dev.live=True
  with self.assertRaisesRegex(RuntimeError,'game is running'):self.runpub()
  self.assertFalse(self.dev.install_calls);self.assertFalse((self.root/pub.m.LOCK).exists())
  self.assertEqual((self.root/'installed.apk').read_bytes(),self.oldapk.read_bytes())
 def test_failed_install_restores_original_directories_profile_and_APK(self):
  def fail(*args):self.install(*args);raise RuntimeError('qualified installer failed after APK update')
  # Restoration invokes the same accepted installer function, allowed once resolved.
  def once(*args):
   if not self.dev.install_calls:return fail(*args)
   return self.install(*args)
  self.assert_restored(self.runpub(install=once))
 def test_unresolved_installer_never_starts_second_install_or_claims_restored(self):
  self.addCleanup(setattr,pub.installer,'UNRESOLVED',False)
  def unresolved(*args):
   self.install(*args);pub.installer.UNRESOLVED=True;raise RuntimeError('official installer unresolved')
  d=self.runpub(install=unresolved);self.assertFalse(d['committed']);self.assertTrue(d['cleanup_errors'])
  self.assertEqual(len(self.dev.install_calls),1);self.assertNotIn('original_restored',d)
  self.assertTrue((self.root/pub.m.LOCK).exists())
 def test_second_round_failure_restores_old_but_preserves_new_latest_save(self):
  def fail(dev,x,out,n):
   result=self.round(dev,x,out,n)
   if n==2:
    (self.root/'files/runtime/save/hero').write_bytes(b'NEW FRESH ORIGINAL SAVE2')
    raise RuntimeError('second ordinary validation failed after original save')
   return result
  self.assert_restored(self.runpub(round=fail))
  self.assertEqual((self.root/'files/runtime/save/hero').read_bytes(),b'NEW FRESH ORIGINAL SAVE2')
 def test_foreign_profile_write_is_not_overwritten_during_rollback(self):
  def conflict(dev,x,out,n):
   (self.root/pub.PROFILE).write_bytes(b'external edit');raise RuntimeError('external conflict')
  d=self.runpub(round=conflict);self.assertFalse(d['committed']);self.assertTrue(d['cleanup_errors'])
  self.assertEqual((self.root/pub.PROFILE).read_bytes(),b'external edit');self.assertTrue((self.root/pub.m.LOCK).exists())
 def test_unowned_PID_preserved_no_force_stop_or_second_install(self):
  def unowned(dev,x,out,n):dev.live=True;raise RuntimeError('replacement PID')
  d=self.runpub(round=unowned);self.assertTrue(d['cleanup_errors']);self.assertEqual(len(self.dev.install_calls),1)
  self.assertTrue((self.root/pub.m.LOCK).exists())
 def test_shader_external_write_not_mislabeled_restored(self):
  def wrong(dev,x,out,n):
   (self.root/'files/runtime/shaders/.chain').write_text('changed elsewhere')
   raise RuntimeError('shader external write')
  d=self.runpub(round=wrong);self.assertTrue(d['cleanup_errors']);self.assertNotIn('original_restored',d)
  self.assertEqual((self.root/'files/runtime/shaders/.chain').read_text(),'changed elsewhere')

class OrdinaryObservation(unittest.TestCase):
 def test_same_resource_policy_without_actual_Java_profile_message_is_not_accepted(self):
  class Observe:
   def stopped(self):pass
   def adb(self,*args):
    if args[:3]==('shell','am','start'):self.launch=args;return 'Status: ok'
    if args[0]=='logcat':self.log_args=args;return self.message
    return '12345'
   def app(self,*args):return '[vulkan] device=Turnip Adreno (TM) 735 x\n[vulkan loader] custom=x\n[native resources] fixed table installed,\n[shared pool] immutable startup shared;'
  current={'valid':True,'world_input_ready':True,'guest_heap':{'shared':True},
   'pool_windows':{'window_bytes':1048576},'shared_lazy_windows':{'active':True},
   'shared_image_raw_observe':{'enabled_policy':False},'driver_constants':{'span_reference':True},
   'pixel_local':{'enabled':True,'shared_readonly_ds':False}}
  for message,accepted in [('Desktop launch: approved',True),('Desktop launch: original platform defaults; no installed profile',False)]:
   with self.subTest(accepted=accepted),tempfile.TemporaryDirectory()as tmp:
    dev=Observe();dev.message=message;x={'p':{'serial':'HOST'},'profile_values':{'name':'approved'}}
    def attach(argv,**kwargs):
     self.assertIn('--attach',argv);Path(argv[argv.index('--report')+1]).write_text(json.dumps({'pid':12345,'original_hud':True}))
    with patch.object(pub.subprocess,'run',side_effect=attach),patch.object(pub,'state',return_value=current),patch.object(pub,'save_owned',return_value={'host_only_fake_save':True})as saved:
     if accepted:result=pub.ordinary_round(dev,x,Path(tmp)/'round',1);self.assertTrue(result['shared_native_policy_observed'])
     else:
      with self.assertRaisesRegex(RuntimeError,'Java profile consumption'):pub.ordinary_round(dev,x,Path(tmp)/'round',1)
     saved.assert_called_once()
    self.assertNotIn('--ez',dev.launch);self.assertNotIn('--es',dev.launch)
    self.assertIn('android.intent.category.LAUNCHER',dev.launch);self.assertIn('--pid=12345',dev.log_args)

class Profile(unittest.TestCase):
 def good(self):
  x={**pub.FIXED,**{k:'a'*64 if k.endswith('_sha256')else'value'for k in pub.IDENTITY_KEYS},
     'receipt_sha256':'b'*64};x['native_table']='native-one-resources/native-layouts.bin';return x
 def test_bounded_standard_properties_without_dynamic_settings(self):
  data=pub.properties(self.good());self.assertLess(len(data),16384);self.assertIn(b'pool_window_bytes=1048576\n',data)
 def test_unknown_dynamic_setting_path_escape_wrong_policy_and_hash_refused(self):
  for k,v in [('scale_3d','.60'),('native_table','../outside'),('pool_window_bytes','16777216'),
               ('driver_sha256','bad'),('device_fingerprint','line\nattack'),('name','x'*65),('name','😀'*33)]:
   with self.subTest(key=k),self.assertRaises(RuntimeError):pub.properties({**self.good(),k:v})

if __name__=='__main__':unittest.main()
