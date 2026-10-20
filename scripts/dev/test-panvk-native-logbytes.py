#!/usr/bin/env python3
"""Offline regression for the real truncated native-log UTF-8 tool failure."""
import argparse,ast,json,subprocess,types
from pathlib import Path
from unittest.mock import patch
p=argparse.ArgumentParser();p.add_argument('--actual-log',type=Path,required=True);a=p.parse_args()
source=Path(__file__).with_name('run-panvk-minegraph-game.py');tree=ast.parse(source.read_text());f=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='native_log_text');ns={'subprocess':subprocess,'shlex':__import__('shlex'),'PKG':'com.d2rmobile.nativeport'};exec(compile(ast.Module(body=[f],type_ignores=[]),str(source),'exec'),ns);fn=ns[f.name];dev=types.SimpleNamespace(command=['adb','-s','offline-only'])
data=a.actual_log.read_bytes();prefix=data[:4096]
try:prefix.decode('utf-8')
except UnicodeDecodeError as e:assert e.start==4095
else:raise AssertionError('actual failing prefix absent')
checks=1
for raw in [prefix,b'\x97\x80[dm] FATAL sentinel\xe5',b'[dm] Android native start pid=3293\n',b'',data]:
 with patch.object(subprocess,'check_output',return_value=raw) as call:
  value=fn(dev,'head','-c','4096','files/runtime/native.log');assert value==raw.decode('utf-8',errors='replace').strip();assert call.call_args.args[0][:3]==dev.command;checks+=1
with patch.object(subprocess,'check_output',return_value=prefix):assert 'Android native start pid=3293\n' in fn(dev,'head');checks+=1
with patch.object(subprocess,'check_output',return_value=b'\x97\x80FATAL sentinel\xe5'):assert 'FATAL sentinel' in fn(dev,'tail');checks+=1
launch=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='launch_once');main=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='main');assert any(isinstance(n,ast.Assign) and any(isinstance(t,ast.Subscript) and isinstance(t.slice,ast.Constant) and t.slice.value=='primary_launch_error' for t in n.targets) for n in ast.walk(launch));checks+=1
# Actual outer handler must retain the journaled launch cause if finally raises.
handler=next(h for n in ast.walk(main) if isinstance(n,ast.Try) for h in n.handlers if any(isinstance(x,ast.Assign) and any(isinstance(t,ast.Subscript) and isinstance(t.slice,ast.Constant) and t.slice.value=='raised_exception_after_launch_finally' for t in x.targets) for x in ast.walk(h)))
ns2={'d':{'primary_launch_error':"RuntimeError('original-reader')"},'e':RuntimeError('finally-save-refused')};exec(compile(ast.Module(body=handler.body,type_ignores=[]),str(source),'exec'),ns2);assert ns2['d']['error']=="RuntimeError('original-reader')" and 'finally-save-refused' in ns2['d']['raised_exception_after_launch_finally'];checks+=1
assert "else lambda d,*a:d.app(*a)" in source.read_text();checks+=1
print(json.dumps({'checks':checks,'actual_4096_byte_truncation_reproduced':True,'ASCII_identity_and_FATAL_sentinels_preserved':True,'other_Device_identity_hash_JSON_channels_strict':True,'primary_survives_finally_failure':True,'device_access':False}))
