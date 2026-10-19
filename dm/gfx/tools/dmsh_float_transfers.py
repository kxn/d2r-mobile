# Scalar float bit-pattern transfers for the experimental fragment precision pass.
# Integer consumers retain their original values; only float Phi/Select copies clone.
from pathlib import Path
import re,subprocess,json,hashlib

def transform(src,dst):
 normal=dst.with_suffix('.normalized.spv')
 subprocess.run(['spirv-opt','--inline-entry-points-exhaustive','--ssa-rewrite','--simplify-instructions','--eliminate-dead-code-aggressive',str(src),'-o',str(normal)],check=True,capture_output=True)
 text=subprocess.check_output(['spirv-dis','--raw-id',str(normal)],text=True);lines=text.splitlines();ins=[];defs={};func=None;block=None;entries={};block_starts={};phi_ends={}
 for ix,l in enumerate(lines):
  t=l.split(';',1)[0].split();rid=None
  if not t:continue
  if len(t)>2 and t[1]=='=':rid=t[0];t=t[2:]
  op,a=t[0],t[1:]
  if op=='OpFunction':func=rid;block=None
  if op=='OpLabel':
   block=rid;block_starts[block]=ix+1;phi_ends[block]=ix+1
   if func not in entries:entries[func]=ix+1
  if block and op=='OpPhi':phi_ends[block]=ix+1
  if block and op=='OpVariable'and a[1]=='Function'and entries.get(func)==ix:entries[func]=ix+1
  row=(ix,rid,op,a,block,func);ins.append(row)
  if rid:defs[rid]=row
  if op=='OpFunctionEnd':func=block=None
 ints={rid for _,rid,op,a,_,_ in ins if op=='OpTypeInt'and a[0]=='32'}
 floats={rid for _,rid,op,a,_,_ in ins if op=='OpTypeFloat'and a==['32']}
 if len(floats)!=1:return {'skipped':'requires exactly one scalar float32 type'}
 ft=next(iter(floats));nextid=max(int(v[1:])for v in re.findall(r'%\d+',text))+1;memo={};insert={};replace={};counts={'float_phi':0,'float_select':0,'casts_removed':0,'casts_inserted':0}
 def alloc():
  nonlocal nextid
  v='%'+str(nextid);nextid+=1;return v
 def add(pos,line):insert.setdefault(pos,[]).append(line)
 def floating(x,fn):
  key=(x,fn)
  if key in memo:return memo[key]
  ix,rid,op,a,bb,ff=defs[x];assert a[0]in ints,(x,op,a)
  if op=='OpBitcast'and defs[a[1]][3][0]in floats:memo[key]=a[1];return a[1]
  if op=='OpCopyObject':
   v=floating(a[1],fn);memo[key]=v;return v
  v=alloc();memo[key]=v
  if op=='OpPhi':
   operands=[]
   for value,label in zip(a[1::2],a[2::2]):operands.extend([floating(value,fn),label])
   add(phi_ends[bb],f'{v} = OpPhi {ft} '+ ' '.join(operands));counts['float_phi']+=1
  elif op=='OpSelect':
   yes,no=floating(a[2],fn),floating(a[3],fn)
   add(ix+1,f'{v} = OpSelect {ft} {a[1]} {yes} {no}');counts['float_select']+=1
  else:
   at=ix+1 if ff else entries[fn]
   add(at,f'{v} = OpBitcast {ft} {x}');counts['casts_inserted']+=1
  return v
 for ix,rid,op,a,bb,fn in ins:
  if op=='OpBitcast'and a[0]in floats and defs[a[1]][3][0]in ints:
   value=floating(a[1],fn);replace[ix]=f'{rid} = OpCopyObject {ft} {value}';counts['casts_removed']+=1
 result=[]
 for ix,l in enumerate(lines):result.extend(insert.get(ix,[]));result.append(replace.get(ix,l))
 asm=dst.with_suffix('.spvasm');asm.write_text('\n'.join(result)+'\n');raw=dst.with_suffix('.raw.spv')
 subprocess.run(['spirv-as','--preserve-numeric-ids','--target-env','spv1.3',str(asm),'-o',str(raw)],check=True,capture_output=True)
 val=subprocess.run(['spirv-val','--target-env','vulkan1.1',str(raw)],capture_output=True,text=True)
 if val.returncode:raise RuntimeError(val.stderr)
 subprocess.run(['spirv-opt','--simplify-instructions','--eliminate-dead-code-aggressive','--eliminate-dead-const',str(raw),'-o',str(dst)],check=True,capture_output=True)
 subprocess.run(['spirv-val','--target-env','vulkan1.1',str(dst)],check=True,capture_output=True)
 return {'counts':counts,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'sha256':hashlib.sha256(dst.read_bytes()).hexdigest()}

