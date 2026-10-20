#!/usr/bin/env python3
"""Offline checks of actual transaction helpers; no ADB or Vulkan workload."""
import ast,re,shlex
from pathlib import Path
source=Path(__file__).resolve().parents[2]/'run-panvk-gfxr-replay-fixture.py'
tree=ast.parse(source.read_text());defs={n.name:n for n in tree.body if isinstance(n,ast.FunctionDef)}
code=compile(ast.fix_missing_locations(ast.Module(body=[defs['observe']],type_ignores=[])),str(source),'exec')
def fixture(raws,status_uid=10359,actual_uid=10359):
 it=iter(raws);record=[];space={'first':None,'ambiguous':False,'result':{},'re':re,'pkg':'com.d2rmobile.panvkgfxrfixture','save':lambda:None,'pid':lambda name:next(it)}
 def ad(*args):
  record.append(args)
  if args[0:2]==('shell','cat'):return 'Name:\tfixture\nUid:\t'+str(status_uid)+' '+str(status_uid)+'\n'
  assert args==('shell','run-as',space['pkg'],'id','-u');return str(actual_uid)
 space['ad']=ad;exec(code,space);return space,record
s,c=fixture(['44','44','']);assert s['observe']()=='44'and s['first']=='44';s['observe'](False);s['observe'](False);assert not s['ambiguous']
s,c=fixture(['44','45']);s['observe']();s['observe']();assert s['ambiguous']and s['first']=='44'and s['result']['PID_raw']==['44','45']
s,c=fixture(['44 99','44']);s['observe']();s['observe']();assert s['ambiguous']and s['first']is None and not c
s,c=fixture(['44']);s['observe'](False);assert s['first']is None and not c
s,c=fixture(['44'],10359,10360)
try:s['observe']()
except AssertionError:pass
else:raise AssertionError('UID mismatch accepted')
args='--validate --memory-translation none /data/user/0/com.d2rmobile.panvkgfxrfixture/files/capture.gfxr'
assert shlex.split('am start --es args '+shlex.quote(args))[-1]==args
# Cleanup must independently attempt stop even if log reading fails.
final=next(n for n in tree.body if isinstance(n,ast.Try)).finalbody
text=ast.unparse(ast.Module(body=final,type_ignores=[]));assert 'owned package cleanup:'in text and 'production read-only verification:'in text
assert 'game_modules'not in text and "'uninstall', pkg"in text
print('PASS: exact first PID, replacement/multiple sticky rejection, no late adoption, matching ordinary UID, shell args preserved, cleanup phases independent')
