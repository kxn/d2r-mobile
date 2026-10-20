#!/usr/bin/env python3
"""Offline transaction policy checks; never constructs an ADB Device."""
import argparse,ast,importlib.util,json,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('one_native_controller',HERE/'run-game.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
checks=[]
def check(name,ok):
 assert ok,name;checks.append(name)
def rejected(name,fn):
 try:fn()
 except (AssertionError,RuntimeError,ValueError,TypeError,KeyError):checks.append(name);return
 raise AssertionError(name+' accepted')
# Frozen static payload proof covers canonical APK, 6 libs, 28 raw artifacts and five exact deltas.
p=c.proof();check('exact-five-overlay-not-RomFS-tree',set(p['overlay'])==c.EXPECTED_FIVE)
check('single-material-payload135168',p['table']['native_payload_bytes']==135168)
check('raw28',len(p['new']['module_artifacts'])==28)
c.m.common.Device=lambda *a,**kw:(_ for _ in()).throw(AssertionError('offline must not instantiate ADB'))
receipt=c.R/'.local/logs/native-image-window-app-v1-q-root-actual-review.json'
c.fixture_gate(receipt,'755c634376a6bdebde5168bbf0b7cc3e1694c8398bd2f8940a7a51790f8dd9ca');checks.append('exact-physical-AA-ASTC-window-root-receipt')
rejected('missing-fixture-gate',lambda:c.fixture_gate(None,None))
rejected('wrong-fixture-pin',lambda:c.fixture_gate(receipt,'0'*64))
rejected('missing-debugger-gate',lambda:c.debugger_gate(None,None))
owner='a'*64;d={'owner_sha256':owner}
for label in ['overlay','modules','native','driver']:
 check('unowned-'+label+'-rejected',not c.current_owned(label,{},d,p))
expected={c.m.OWNER:owner,**p['overlay']};check('exact-overlay-accepted',c.current_owned('overlay',expected,d,p))
check('extra-RomFS-leaf-rejected',not c.current_owned('overlay',{**expected,'data/00-00000001.data':'b'*64},d,p))
check('bad-table-rejected',not c.current_owned('native',{c.m.OWNER:owner,'native-layouts.bin':'b'*64},d,p))
check('exact-table-accepted',c.current_owned('native',{c.m.OWNER:owner,'native-layouts.bin':p['table']['native_table_sha256']},d,p))
check('replacement-PID-rejected',c.m.claim_pid({},'123','123')=='123')
rejected('multiple-PIDs-rejected',lambda:c.m.claim_pid({},'123 124','123'))
rejected('new-PID-not-adopted',lambda:c.m.claim_pid({},'124','123'))
c.m.NATIVE_TABLE=c.ABS_TABLE;c.m.PACK_SHA=p['table']['native_pack_content_sha256'];c.m.PRODUCER_SHA=p['table']['producer_source_sha256']
cmd=c.m.launch_command('10CG570J59001U1',Path('/tmp/not-executed'))
for name,val in [('--pool-window-bytes','16777216'),('--scale-3d','0.75'),('--game-res','2750x1260'),('--native-resource-table',c.ABS_TABLE),('--native-resource-pack-sha256',c.m.PACK_SHA)]:check('cold-'+name,cmd[cmd.index(name)+1]==val)
check('shared-explicit-no-reference-phase','--shared-guest-heap'in cmd and '--no-shared-guest-heap'not in cmd)
check('installed-table-not-use-proof',not c.accepted_debugger({'debugger_result':{'table_installed':True}}))
check('missing-success-use-rejected',not c.accepted_debugger({'debugger_result':{'native_binding_accepted':False,'owned_debugger_cleanup_complete':True}}))
check('unclean-debugger-rejected',not c.accepted_debugger({'debugger_result':{'native_binding_accepted':True,'owned_debugger_cleanup_complete':False}}))
check('entry-success-and-cleanup-accepted',c.accepted_debugger({'debugger_result':{'native_binding_accepted':True,'owned_debugger_cleanup_complete':True}}))
# These are reused byte-for-byte, not a parallel reimplementation of directory/PID primitives.
old=(c.SOURCE_TOOLS/'reused-transaction.py').read_text();new=(HERE/'transaction.py').read_text()
def funcs(s):return {n.name:ast.get_source_segment(s,n)for n in ast.parse(s).body if isinstance(n,ast.FunctionDef)}
a,b=funcs(old),funcs(new)
for f in ['inventory','rename_dir','claim_pid','read_json_line']:check('reused-original-'+f,a[f]==b[f])
text=(HERE/'run-game.py').read_text()+(HERE/'transaction.py').read_text()
check('no-CPU-profile-or-two-phase-execution','capture-official-cpu.py'not in text and 'for window in (0,16777216)'not in text)
arg=argparse.ArgumentParser();arg.add_argument('--report',type=Path,required=True);opt=arg.parse_args();opt.report.parent.mkdir(parents=True,exist_ok=True);opt.report.write_text(json.dumps({'offline':True,'ADB_access':False,'checks':checks,'count':len(checks),'state':'source policy passed; frozen debugger sourceReady remains execute prerequisite'},indent=2)+'\n');print(len(checks),'offline checks passed')
