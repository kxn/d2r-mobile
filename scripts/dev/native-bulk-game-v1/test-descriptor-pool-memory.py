#!/usr/bin/env python3
"""Host-only narrow RAM contract tests; temporary child artifact metadata is synthetic.

Actual52181 source/host proof exists. Its Android artifacts/root approval are
NOT claimed here. No execute-ready inputs, device calls or captures are made.
"""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE=Path(__file__).resolve().parent;R=Path('/home/dl/d2r-mobile')
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);v=importlib.util.module_from_spec(s);s.loader.exec_module(v);return v
g=load('memory_runner',HERE/'run-game.py');c=load('memory_gate',HERE/'candidate_gate.py')
def rec(p):return {'path':str(p),'sha256':g.sha(p),'bytes':Path(p).stat().st_size}

class Contract(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='synthetic-descriptor-RAM-');self.addCleanup(self.temp.cleanup)
  self.out=Path(self.temp.name)
  self.parentpin=rec(R/'.local/logs/lazy-physical-span-index-88620ec-root-canonical-raw-review.json')
  self.parent=g.read(g.pinned(self.parentpin))
  self.can=copy.deepcopy(g.read(g.pinned(self.parent['canonical_ready'])))
  self.raw=copy.deepcopy(g.read(g.pinned(self.parent['raw_ready'])))
  self.manifest=copy.deepcopy(g.read(g.pinned(self.can['canonical_build_manifest'])))
  self.sourcepin=rec(R/'.local/logs/descriptor-pool-block-512-v1/source-ready-agent.json')
  self.sourcereview=rec(R/'.local/logs/descriptor-pool-block-512-v1/root-source-review.json')
  self.child=copy.deepcopy(self.parent);self.child.update(runtime=c.DESCRIPTOR_CANDIDATE,base=c.DESCRIPTOR_PARENT,source_ready=self.sourcepin)
  actual_can=g.read(R/'.local/logs/descriptor-pool-block-512-52181dc-canonical-source-ready-agent.json')
  self.child['actual_APP_preprocessed_units']=7
  self.child['raw28_files_hash_verified']=True
  self.can['actual_APP_preprocessed_translation_units_total']=7
  self.can['additional_native_test_APP_closure']=actual_can['additional_native_test_APP_closure']
  self.can.update(commit=c.DESCRIPTOR_CANDIDATE,base=c.DESCRIPTOR_PARENT,source_only_candidate=self.sourcepin,root_source_reviews=[self.sourcereview])
  self.manifest['source_revision']=c.DESCRIPTOR_CANDIDATE;self.raw['source_commit']=c.DESCRIPTOR_CANDIDATE
  self.review={'status':'ROOT_REVIEWED_DESCRIPTOR_POOL_PHYSICAL_CAPACITY_MEMORY_COMPARISON',
   'parent_runtime':c.DESCRIPTOR_PARENT,'candidate_runtime':c.DESCRIPTOR_CANDIDATE,'base_runtime':c.DESCRIPTOR_PARENT,
   'physical_descriptor_pool_max_sets':{'parent':2048,'candidate':512},'retention_budget':2048,
   'production_capacity_only':True,'shader_changes':False,'game_changes':False,'driver_changes':False,
   'parent_artifact_review':self.parentpin,'candidate_source_review':self.sourcereview}
  self.p=g.read(R/'.local/build/private-image-budget-74d7e17-Q735-pair-v1/root-execute/PARENT1.json')
  for key in ('official_cpu_aa','performance_tools','official_l1i_record'):self.p.pop(key,None)
  self.p.update(baseline=rec(R/'.local/logs/current-sustained-886-fresh-baseline-v2/baseline.json'),
   original_apk=self.can['canonical_APK']['archive'],original_host_elf=self.parent['host_ELF_SHA256'],execution_ready=False)
  self.p['native_memory_validation']={'role':'candidate','scheduler_helper':rec(HERE/'capture-sustained-scheduler.py')}
 def write(self,name,data):
  p=self.out/name;p.write_text(json.dumps(data));return rec(p)
 def prepare(self,role=None):
  if role:self.p['native_memory_validation']['role']=role
  self.can['canonical_build_manifest']=self.write('manifest.json',self.manifest)
  self.child['canonical_ready']=self.write('synthetic-child-canonical.json',self.can)
  self.child['raw_ready']=self.write('synthetic-child-raw.json',self.raw)
  self.review['candidate_artifact_review']=self.write('synthetic-child-artifact-review.json',self.child)
  self.p['native_memory_validation']['root_review']=self.write('synthetic-memory-review.json',self.review)
  card=self.parent if self.p['native_memory_validation']['role']=='parent'else self.child
  self.p.update(runtime_commit=card['runtime'],canonical=card['canonical_ready'],raw=card['raw_ready'])
  self.p['native_candidate_validation']={'root_review':self.parentpin if card is self.parent else self.review['candidate_artifact_review']}
  self.p['native_host_experiment']={'kind':'resource-preparation','base_runtime':card['base']}
 def invoke(self):
  self.prepare();token=c.validate_memory(self.p,g.pinned,g.read,g.require)
  g.builtin_vma_policy(self.p,memory_runtime=token)
  return c.validate(self.p,g.pinned,g.read,g.require,memory_runtime=token)
 def test_parent_and_candidate_use_original_common_artifact_checks(self):
  for role in ('parent','candidate'):
   with self.subTest(role=role):
    self.p['native_memory_validation']['role']=role
    self.assertEqual(self.invoke(),c.DESCRIPTOR_PARENT if role=='parent'else c.DESCRIPTOR_CANDIDATE)
 def test_exact_parent_child_and_physical_retention_contract(self):
  for key,value in [('parent_runtime','0'*40),('candidate_runtime','1'*40),('base_runtime','2'*40),
      ('physical_descriptor_pool_max_sets',{'parent':2048,'candidate':1024}),('retention_budget',512),
      ('production_capacity_only',False),('shader_changes',True),('game_changes',True),('driver_changes',True)]:
   with self.subTest(key=key):
    old=self.review[key];self.review[key]=value
    with self.assertRaises(RuntimeError):self.invoke()
    self.review[key]=old
 def test_source_revision_and_source_review_not_optional(self):
  for target,key,value in [(self.child,'base','a'*40),(self.can,'commit','b'*40),
      (self.raw,'source_commit','c'*40),(self.can,'root_source_reviews',[])]:
   old=target[key];target[key]=value
   with self.subTest(key=key),self.assertRaises(RuntimeError):self.invoke()
   target[key]=old
 def test_root_hash_and_missing_actual_artifacts_are_refused(self):
  self.prepare();self.p['native_memory_validation']['root_review']['sha256']='0'*64
  with self.assertRaises(RuntimeError):c.validate_memory(self.p,g.pinned,g.read,g.require)
  self.prepare();child=g.read(g.pinned(self.review['candidate_artifact_review']))
  child['canonical_ready']={'path':str(self.out/'ACTUAL-CANONICAL-NOT-BUILT.json'),'sha256':'1'*64}
  self.review['candidate_artifact_review']=self.write('missing-actual-card.json',child)
  self.p['native_memory_validation']['root_review']=self.write('missing-actual-review.json',self.review)
  with self.assertRaises(FileNotFoundError):c.validate_memory(self.p,g.pinned,g.read,g.require)
 def test_selected_role_runtime_and_artifacts_cannot_mix(self):
  self.prepare();self.p['canonical']=self.parent['canonical_ready']
  with self.assertRaises(RuntimeError):c.validate_memory(self.p,g.pinned,g.read,g.require)
  self.prepare();self.p['runtime_commit']=c.DESCRIPTOR_PARENT
  with self.assertRaises(RuntimeError):c.validate_memory(self.p,g.pinned,g.read,g.require)
 def test_no_profiling_or_legacy_scope_substitutions(self):
  for key in ('official_cpu_aa','performance_tools','sustained_play','native_functional_validation',
      'prior_actual_game_qualification','readonly_memory_snapshot','startup_allocation_capture','official_gpu_tools'):
   self.p[key]={}
   with self.subTest(key=key),self.assertRaises(RuntimeError):self.invoke()
   self.p.pop(key)
 def test_current886_restore_and_original_game_outputs_required(self):
  old=self.p['original_host_elf'];self.p['original_host_elf']='0'*64
  with self.assertRaises(RuntimeError):self.invoke()
  self.p['original_host_elf']=old;self.p['pool_window_bytes']=0
  with self.assertRaises(RuntimeError):self.invoke()
 def test_full_common_raw_and_effective_flags_guard_remain(self):
  original=self.raw['28_deployment_file_hashes']['main.patched.bin']
  self.raw['28_deployment_file_hashes']['main.patched.bin']='0'*64
  with self.assertRaises(RuntimeError):self.invoke()
  self.raw['28_deployment_file_hashes']['main.patched.bin']=original
  self.manifest['host_extra_compile_options']=['-ffast-math']
  with self.assertRaises(RuntimeError):self.invoke()
 def test_no_input_forged_validation_token_or_unscoped_VMA(self):
  self.prepare()
  with self.assertRaises(RuntimeError):c.validate(self.p,g.pinned,g.read,g.require)
  with self.assertRaises(RuntimeError):g.builtin_vma_policy(self.p)
  self.p.pop('native_memory_validation')
  with self.assertRaises(RuntimeError):g.builtin_vma_policy(self.p)
 def test_actual_seven_closures_only_in_validated_child_RAM_scope(self):
  rootpin=rec(R/'.local/logs/descriptor-pool-block-512-52181dc-root-canonical-raw-review.json')
  card=g.read(g.pinned(rootpin));p=copy.deepcopy(self.p)
  p.update(runtime_commit=c.DESCRIPTOR_CANDIDATE,canonical=card['canonical_ready'],raw=card['raw_ready'],
   native_candidate_validation={'root_review':rootpin},
   native_host_experiment={'kind':'resource-preparation','base_runtime':c.DESCRIPTOR_PARENT})
  p['native_memory_validation']['root_review']=rec(R/'.local/logs/descriptor-pool-block-512-52181dc-root-memory-comparison-review.json')
  token=c.validate_memory(p,g.pinned,g.read,g.require)
  self.assertEqual(c.validate(p,g.pinned,g.read,g.require,memory_runtime=token),c.DESCRIPTOR_CANDIDATE)
  # The common CPU gate now accepts the same honest seven-unit artifact card.
  c.reviewed_artifacts(p,card,g.pinned,g.read,g.require)
  invalid=copy.deepcopy(card);invalid['actual_APP_preprocessed_units']=6
  with self.assertRaisesRegex(RuntimeError,'six APP units plus'):
   c.reviewed_artifacts(p,invalid,g.pinned,g.read,g.require,memory_validated=True)

class Capture(unittest.TestCase):
 def exercise(self,failure=None):
  with tempfile.TemporaryDirectory() as folder:
   out=Path(folder);pid=['123'];p={'serial':'UNIT','runtime_commit':c.DESCRIPTOR_CANDIDATE,
    '_descriptor_memory_runtime':c.DESCRIPTOR_CANDIDATE,'new_apk':'apk','baseline_data':{'chain':'9507875007c6'},
    'native_memory_validation':{'scheduler_helper':rec(HERE/'capture-sustained-scheduler.py')}}
   state={'time_ns':1,'valid':True,'world_input_ready':True,'scale_3d':.60,
    'game_output':{'width':2750,'height':1260},'surface_output':{'width':2750,'height':1260},
    'guest_heap':{'shared':True},'pool_windows':{'window_bytes':1048576},
    'private_image_cache':{'budget_bytes':536870912},'shared_lazy_windows':{'active':True},
    'shared_image_raw_observe':{'enabled_policy':False},'driver_constants':{'span_reference':True}}
   class Dev:
    def adb(self,*args):return pid[0]
   def child(command,**kwargs):
    self.assertEqual(command[command.index('--seconds')+1],'8');self.assertNotIn('simpleperf',' '.join(command))
    if failure=='tool':raise subprocess.CalledProcessError(1,command)
    trace=Path(command[command.index('--output')+1]);trace.write_bytes(b'official mocked trace')
    after=copy.deepcopy(state);after['time_ns']=1 if failure=='stale'else 2
    if failure=='policy':after['private_image_cache']['budget_bytes']=67108864
    data={'pid':999 if failure=='identity'else 123,'seconds_requested':8,'apk_sha256':'apk',
     'shader_chain':'9507875007c6','script_sha256':p['native_memory_validation']['scheduler_helper']['sha256'],
     'state_reader_sha256':g.sha(HERE.parent/'game_state_json.py'),'trace_sha256':g.sha(trace),'before':state,'after':after}
    trace.with_suffix('.json').write_text(json.dumps(data))
    if failure=='PID':pid[0]='replacement'
   d={}
   with patch.object(g.subprocess,'run',side_effect=child):
    if failure:
     with self.assertRaises((RuntimeError,subprocess.CalledProcessError)):
      g.capture_memory_cpu_window(Dev(),'123',d,out/'phase.json',out,p)
     self.assertNotIn('descriptor_memory_cpu_window',d)
    else:
     g.capture_memory_cpu_window(Dev(),'123',d,out/'phase.json',out,p)
     self.assertTrue(d['descriptor_memory_cpu_window']['complete'])
 def test_one_existing_official_eight_second_window(self):self.exercise()
 def test_tool_identity_policy_freshness_and_PID_fail_closed(self):
  for failure in ('tool','identity','policy','stale','PID'):
   with self.subTest(failure=failure):self.exercise(failure)

if __name__=='__main__':unittest.main(verbosity=2)
