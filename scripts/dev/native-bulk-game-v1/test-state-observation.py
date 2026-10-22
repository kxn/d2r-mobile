#!/usr/bin/env python3
"""Test the real sustained endpoint/full snapshot readers with original app strip."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

HERE = Path(__file__).resolve().parent

def load(name, path):
    spec=importlib.util.spec_from_file_location(name,path)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value

s=load('observed_sustained',HERE/'sustained_play.py')
g=load('observed_bulk',HERE/'run-game.py')

def require(ok,message):
    if not ok:raise RuntimeError(message)

class Readers(unittest.TestCase):
    def setUp(self):
        self.state={'valid':True,'world_input_ready':True,'time_ns':42,'area':1,'x':5225,'y':6289,
            'scale_3d':.65,'game_output':{'width':2750,'height':1260},
            'surface_output':{'width':2750,'height':1260},'guest_heap':{'shared':True},
            'pool_windows':{'window_bytes':1048576},'shared_lazy_windows':{'active':True},
            'shared_image_raw_observe':{'enabled_policy':False},'driver_constants':{'span_reference':True},
            'private_image_cache':{'budget_bytes':536870912}}
        self.p={'_sustained_current':True,'pool_window_bytes':1048576}
        self.m=SimpleNamespace(PKG='pkg',require=require)

    def device(self,text,pid='owned',full=False):
        case=self
        class Dev:
            def adb(self,*args):
                case.assertEqual(args,('shell','pidof pkg || true'));return pid
            def app(self,*args):
                case.assertEqual(args,('sh','-c','set -e; '+('cat "$1"' if full else 'tail -n 2 "$1"')+
                                     '; printf %s '+s.state_reader._END,'sh','files/runtime/game-state.jsonl'))
                return (text+s.state_reader._END).strip()
        return Dev()

    def test_endpoint_uses_last_committed_state_with_partial_tail(self):
        text=json.dumps(self.state)+'\n{"partial":'
        self.assertEqual(s.endpoint_state(self.device(text),'owned',self.p,self.m),self.state)

    def test_endpoint_does_not_hide_malformed_complete_or_current_not_ready(self):
        good=json.dumps(self.state)+'\n'
        with self.assertRaises(json.JSONDecodeError):
            s.endpoint_state(self.device(good+'{bad}\n{"partial":'),'owned',self.p,self.m)
        current=copy.deepcopy(self.state);current['world_input_ready']=False
        with self.assertRaises(RuntimeError):
            s.endpoint_state(self.device(json.dumps(current)+'\n{"partial":'),'owned',self.p,self.m)
        with self.assertRaises(ValueError):
            s.endpoint_state(self.device('{"partial":'),'owned',self.p,self.m)

    def test_endpoint_PID_check_still_precedes_read(self):
        with self.assertRaisesRegex(RuntimeError,'PID changed'):
            s.endpoint_state(self.device('unused',pid='replacement'),'owned',self.p,self.m)

    def test_full_snapshot_keeps_raw_partial_evidence_but_returns_complete_state(self):
        text=json.dumps(self.state)+'\n{"partial":'
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)
            self.assertEqual(g.read_state(self.device(text,full=True),out,'state'),self.state)
            self.assertEqual((out/'state.jsonl').read_text(),text)
            with self.assertRaises(json.JSONDecodeError):
                g.read_state(self.device(text+'}\n',full=True),out,'malformed')

if __name__=='__main__':unittest.main(verbosity=2)
