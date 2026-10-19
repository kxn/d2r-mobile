# Minimal offline policy using SPIRV-Tools normalized IR and CFG postdominators.
# Approximation is restricted by data AND control dependencies of non-color effects.
# Opt-in compiler experiment; no scene, shader-key or game-state conditions.
from pathlib import Path
import subprocess,re
FLOAT_OPS={'OpFAdd','OpFSub','OpFMul','OpFNegate','OpDot','OpVectorTimesScalar','OpMatrixTimesScalar','OpVectorTimesMatrix','OpMatrixTimesVector','OpMatrixTimesMatrix','OpOuterProduct'}
SHUFFLES={'OpGroupNonUniformShuffle','OpGroupNonUniformShuffleXor','OpGroupNonUniformShuffleUp','OpGroupNonUniformShuffleDown','OpGroupNonUniformQuadBroadcast','OpGroupNonUniformQuadSwap'}
MATH={'Fma','FAbs','FMin','FMax','NMin','NMax','FClamp','NClamp','FMix','Floor','Ceil','Trunc','Fract','Round','RoundEven','Sqrt','InverseSqrt','Exp','Exp2','Log','Log2','Pow','Sin','Cos','Tan','Asin','Acos','Atan','Atan2','Sinh','Cosh','Tanh','Radians','Degrees','Sign','FSign','Step','SmoothStep','Length','Distance','Cross','Normalize','Reflect','Refract','FaceForward'}
TARGET_MATH={'Fma','FAbs','FMin','FMax','NMin','NMax','FClamp','NClamp','FMix'}
TRANSFER={'OpLoad','OpCopyObject','OpPhi','OpSelect','OpCompositeExtract','OpCompositeConstruct','OpCompositeInsert','OpVectorShuffle','OpBitcast'}|SHUFFLES
CMP={'OpFOrdEqual','OpFUnordEqual','OpFOrdNotEqual','OpFUnordNotEqual','OpFOrdLessThan','OpFUnordLessThan','OpFOrdGreaterThan','OpFUnordGreaterThan','OpFOrdLessThanEqual','OpFUnordLessThanEqual','OpFOrdGreaterThanEqual','OpFUnordGreaterThanEqual','OpIsNan','OpIsInf','OpIsFinite','OpIsNormal','OpSignBitSet'}

def transform(src,dst):
 normal=dst.with_suffix('.normalized.spv');subprocess.run(['spirv-opt','--inline-entry-points-exhaustive','--ssa-rewrite','--simplify-instructions','--eliminate-dead-code-aggressive',str(src),'-o',str(normal)],check=True,capture_output=True)
 text=subprocess.check_output(['spirv-dis','--raw-id',str(normal)],text=True);lines=text.splitlines();ins=[];defs={};blocks={};functions={};bb=fn=None
 for ix,l in enumerate(lines):
  t=l.split(';',1)[0].split();rid=None
  if not t:continue
  if len(t)>2 and t[1]=='=':rid=t[0];t=t[2:]
  op,a=t[0],t[1:]
  if op=='OpFunction':fn=rid
  if op=='OpLabel':bb=rid;blocks[bb]={'function':fn,'instructions':[]};functions.setdefault(fn,[]).append(bb)
  row=(rid,op,a,bb);ins.append(row)
  if rid:defs[rid]=row
  if bb:blocks[bb]['instructions'].append(row)
  if op=='OpFunctionEnd':bb=fn=None
 if any(op=='OpFunctionCall'for _,op,_,_ in ins):return {'skipped':'un-inlined function call'}
 if any(op=='OpMemberDecorate'and 'BuiltIn'in a for _,op,a,_ in ins):return {'skipped':'member builtin alias proof required'}
 floats={rid for rid,op,a,_ in ins if op=='OpTypeFloat'and a==['32']}
 types={rid:1 for rid,op,a,_ in ins if op in ['OpTypeBool','OpTypeInt','OpTypeFloat']}
 float_types=set(floats)
 for rid,op,a,_ in ins:
  if op=='OpTypeVector'and a[0]in types:
   types[rid]=int(a[1])
   if a[0]in floats:float_types.add(rid)
 widths={rid:types[a[0]]for rid,op,a,_ in ins if rid and not op.startswith('OpType')and a and a[0]in types}
 float_values={rid for rid,op,a,_ in ins if rid in widths and a[0]in float_types}
 full=lambda x:(1<<widths[x])-1 if x in widths else 0
 constants={rid:int(a[1])for rid,op,a,_ in ins if op=='OpConstant'and defs[a[0]][1]=='OpTypeInt'}
 precise={a[0]for _,op,a,_ in ins if op=='OpDecorate'and a[1]=='NoContraction'}
 builtins={a[0]for _,op,a,_ in ins if op=='OpDecorate'and a[1]=='BuiltIn'}
 locations={a[0]for _,op,a,_ in ins if op=='OpDecorate'and a[1]=='Location'}
 imports={rid for rid,op,a,_ in ins if op=='OpExtInstImport'and a==['"GLSL.std.450"']}
 controllers={b:set()for b in blocks};conditions={}
 for fn,bs in functions.items():
  exit='exit:'+fn;nodes=set(bs)|{exit};successors={exit:set()}
  for b in bs:
   rows=blocks[b]['instructions'];term=next((r for r in reversed(rows)if r[1]not in ['OpFunctionEnd','OpLine','OpNoLine']),None);_,op,a,_=term
   if op=='OpBranch':succ=[a[0]]
   elif op=='OpBranchConditional':succ=a[1:3];conditions[b]=a[0]
   elif op=='OpSwitch':succ=[a[1],*a[3::2]];conditions[b]=a[0]
   elif op in ['OpReturn','OpReturnValue','OpKill','OpTerminateInvocation','OpUnreachable']:succ=[exit]
   else:return {'skipped':'unsupported CFG terminator '+op}
   if any(x not in nodes for x in succ):return {'skipped':'CFG edge outside function'}
   successors[b]=set(succ)
  reachable={exit}
  while True:
   new=reachable|{b for b in bs if successors[b]&reachable}
   if new==reachable:break
   reachable=new
  if reachable!=nodes:return {'skipped':'CFG has blocks without exit reachability'}
  pd={b:({exit}if b==exit else set(nodes))for b in nodes}
  changed=True
  while changed:
   changed=False
   for b in bs:
    v={b}|set.intersection(*(pd[s]for s in successors[b]))
    if v!=pd[b]:pd[b]=v;changed=True
  immediate={}
  for b in bs:
   strict=pd[b]-{b};nearest=[p for p in strict if strict-{p}<=pd[p]]
   if len(nearest)!=1:return {'skipped':'ambiguous immediate postdominator'}
   immediate[b]=nearest[0]
  for b in bs:
   if b not in conditions:continue
   for s in successors[b]:
    cursor=s;seen=set()
    while cursor!=immediate[b]:
     if cursor==exit or cursor in seen:return {'skipped':'invalid control dependency chain'}
     seen.add(cursor);controllers[cursor].add(b);cursor=immediate[cursor]
 def base(x):
  seen=set()
  while x not in seen:
   seen.add(x);d=defs.get(x)
   if not d or d[1]not in ['OpAccessChain','OpInBoundsAccessChain','OpPtrAccessChain','OpCopyObject']:break
   x=d[2][1]
  return x
 stores={}
 for _,op,a,b in ins:
  if op=='OpStore':stores.setdefault(base(a[0]),[]).append((a[1],b))
 protected={};queue=[];marked_blocks=set()
 def protect(x,mask=None):
  if x not in widths:return
  mask=full(x)if mask is None else mask&full(x);prior=protected.get(x,0)
  if mask&~prior:protected[x]=prior|mask;queue.append(x)
 def context(b):
  if b is None or b in marked_blocks:return
  marked_blocks.add(b)
  for control in controllers[b]:protect(conditions[control]);context(control)
 for rid,op,a,b in ins:
  sources=[x for x in a[1 if rid else 0:]if x in float_values]
  if op not in SHUFFLES and op.startswith(('OpImage','OpDPdx','OpDPdy','OpFwidth','OpGroupNonUniform')):
   for v in sources:protect(v)
   context(b)
  if rid in precise:protect(rid);context(b)
  if op in ['OpKill','OpTerminateInvocation','OpDemoteToHelperInvocation','OpReturn','OpReturnValue']:context(b)
  if op=='OpStore':
   root=base(a[0]);d=defs.get(root);storage=d[2][1]if d and d[1]=='OpVariable'else None
   if root in builtins or storage not in ['Function','Output']:
    protect(a[1]);context(b)
   elif storage=='Output':
    ptr=defs.get(a[0]);component=None
    if ptr and ptr[1]in ['OpAccessChain','OpInBoundsAccessChain']and len(ptr[2])==3 and ptr[2][1]==root:component=constants.get(ptr[2][2])
    if root in locations and a[0]==root and widths.get(a[1])==4:protect(a[1],8);context(b)
    elif root in locations and component in [0,1,2]:pass
    else:protect(a[1]);context(b)
  elif sources and op not in CMP:
   allowed=op in FLOAT_OPS|TRANSFER|{'OpFDiv','OpFRem','OpFMod'}or(op=='OpExtInst'and a[1]in imports and a[2]in MATH)
   # Genuine integer/bit observations retain all upstream float precision.
   if rid not in float_values or not allowed:
    for v in sources:protect(v)
 while queue:
  x=queue.pop();_,op,a,b=defs[x];mask=protected[x];context(b)
  if op=='OpCompositeExtract'and len(a)==3 and a[1]in widths:protect(a[1],mask<<int(a[2]));continue
  if op=='OpVectorShuffle':
   aw=widths.get(a[1],0)
   for lane,source in enumerate(a[3:]):
    if mask&(1<<lane):
     n=int(source)
     if n!=0xffffffff:protect(a[1]if n<aw else a[2],1<<(n if n<aw else n-aw))
   continue
  if op=='OpCompositeConstruct':
   shift=0
   for v in a[1:]:w=widths.get(v,0);protect(v,mask>>shift);shift+=w
   continue
  if op=='OpCompositeInsert'and len(a)==4:
   lane=int(a[3]);protect(a[2],mask&~(1<<lane))
   if mask&(1<<lane):protect(a[1])
   continue
  if op=='OpPhi':
   for value,pred in zip(a[1::2],a[2::2]):
    protect(value,mask);context(pred)
    if pred in conditions:protect(conditions[pred])
   continue
  if op=='OpSelect':protect(a[1]);protect(a[2],mask);protect(a[3],mask);continue
  if op in SHUFFLES:protect(a[2],mask);continue
  if op in FLOAT_OPS|{'OpCopyObject'}or(op=='OpExtInst'and a[1]in imports and a[2]in TARGET_MATH):
   for v in a[3:]if op=='OpExtInst'else a[1:]:
    if v in widths:protect(v,mask if widths[v]==widths[x]else full(v))
   continue
  if op=='OpLoad':
   for value,store_block in stores.get(base(a[1]),[]):protect(value);context(store_block)
  for v in a[1:]:protect(v)
 targets=[rid for rid,op,a,_ in ins if rid in float_values and not protected.get(rid,0)and rid not in precise and(op in FLOAT_OPS or(op=='OpExtInst'and a[1]in imports and a[2]in TARGET_MATH))]
 existing={a[0]for _,op,a,_ in ins if op=='OpDecorate'and a[1]=='RelaxedPrecision'};targets=sorted(set(targets)-existing)
 at=next(i for i,l in enumerate(lines)if '= OpType'in l);lines[at:at]=['OpDecorate '+rid+' RelaxedPrecision'for rid in targets]
 asm=dst.with_suffix('.spvasm');asm.write_text('\n'.join(lines)+'\n');subprocess.run(['spirv-as','--preserve-numeric-ids','--target-env','spv1.3',str(asm),'-o',str(dst)],check=True,capture_output=True);subprocess.run(['spirv-val','--target-env','vulkan1.1',str(dst)],check=True,capture_output=True)
 return {'eligible_arithmetic':len(targets),'eligible_ids':targets,'protected_values':len(protected),'protected_context_blocks':len(marked_blocks),'blocks':len(blocks),'scope':'EXPERIMENT: exact/control contexts preserved for alpha/depth/storage/discard/images/derivatives/precise ops. Colour-only nonlinear inputs and control can receive rounded arithmetic. HDR/underflow visual risk remains; no production deployment.'}
