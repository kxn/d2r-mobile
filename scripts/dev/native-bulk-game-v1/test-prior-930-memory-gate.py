"""Real pinned game acceptance; synthetic negative result mutations, no device."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

HERE=Path(__file__).resolve().parent
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
gate=load('prior930gate',HERE/'candidate_gate.py')
runner=load('prior930runner',HERE/'run-game.py')
INPUT=Path('/home/dl/d2r-mobile/.local/logs/sdk-main-arena-provenance-v1/inputs-ROOT.json')
def read(path): return json.loads(Path(path).read_text())
def require(ok,message):
    if not ok: raise RuntimeError(message)
def pinned(row):
    path=Path(row['path'])
    require(hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256'],'pin changed')
    return path

class Prior930(unittest.TestCase):
    def config(self): return read(INPUT)
    def test_real_original_game_acceptance_allows_only_same_source_memory_flow(self):
        p=self.config()
        self.assertEqual(gate.validate(p,pinned,read,require,readonly_memory_snapshot_validated=True),p['runtime_commit'])
    def test_snapshot_validation_is_mandatory(self):
        with self.assertRaises(RuntimeError):
            gate.validate(self.config(),pinned,read,require)
    def test_failed_original_route_save_or_cleanup_cannot_qualify(self):
        p=self.config()
        prior=read(pinned(p['prior_actual_game_qualification']))
        for key in ['original_game_modules_executed','original_three_routes_completed',
                    'fresh_save2_then_stopped','original_restored','cleanup_complete']:
            q=copy.deepcopy(prior);q[key]=False
            with self.subTest(key=key),self.assertRaises(RuntimeError):
                gate.prior_930_memory_scope(p,q,pinned,read,require)
    def test_actual_transaction_and_inputs_must_agree(self):
        p=self.config();prior=read(pinned(p['prior_actual_game_qualification']))
        for failure in ['cleanup','PID','input_hash','composition']:
            def altered(path):
                d=read(path)
                if str(path).endswith('/shared-b2t-ordered-preflight-930ac8f-functional-Q735-v1/result.json'):
                    if failure=='cleanup': d['cleanup_complete']=False
                    if failure=='PID': d['phases'][0]['first_PID']+=1
                    if failure=='input_hash': d['inputs_sha256']='0'*64
                if str(path).endswith('/shared-b2t-functional-tools-v1/inputs-EXECUTE.json') and failure=='composition':
                    d['phases'][1]['composition']['sha256']='0'*64
                return d
            with self.subTest(failure=failure),self.assertRaises(RuntimeError):
                gate.prior_930_memory_scope(p,prior,pinned,altered,require)
    def test_base_snapshot_completeness_does_not_prove_arena_ownership(self):
        # These are deliberately synthetic tool results, never capture evidence.
        result={'snapshot_complete':True,'inferior_calls':False,
                'owned_PID_TracerPid_zero_after_cleanup':True,'cleanup_errors':[],
                'snapshot':{}}
        with self.assertRaises(RuntimeError): runner.readonly_memory_snapshot_result(result,self.config())
        result['snapshot']['main_arena_origins']={'complete':True,
            'status':'SAME_SDK_STOP_MAIN_ARENA_ORIGINS_COMPLETE','inferior_calls':False,
            'errors':[],'metadata_acquisition_bytes':810}
        runner.readonly_memory_snapshot_result(result,self.config())
        for field,value in [('complete',False),('errors',['missing pointer']),
                            ('metadata_acquisition_bytes',851),('inferior_calls',True)]:
            q=copy.deepcopy(result);q['snapshot']['main_arena_origins'][field]=value
            with self.subTest(field=field),self.assertRaises(RuntimeError):
                runner.readonly_memory_snapshot_result(q,self.config())

if __name__=='__main__': unittest.main()
