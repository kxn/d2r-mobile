"""Conservative offline four-word one-hot UBO selector -> nested conditional CFG (v2).
No shader IDs, bindings or scene identities are used to recognize the pattern.
The symbolic bit-vector proof includes the source's empty selection/Phi mask.
Only readonly non-volatile Uniform scalar groups with exclusively mask uses qualify.
"""
from pathlib import Path
import hashlib, re, subprocess, sys
R=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(R/'.local/tools/ubo-bitproof-python'))
import z3


def parse(text):
 rows=[];defs={};const={};blocks={};cur=None
 for i,line in enumerate(text.splitlines()):
  w=line.split();result=w[0]if len(w)>2 and w[1]=='='else None
  op,a=(w[2],w[3:])if result else (w[0],w[1:])if w and not w[0].startswith(';')else('',[])
  if op=='OpLabel':cur=result;blocks[cur]=[]
  rows.append((line,result,op,a,cur))
  if cur:blocks[cur].append(i)
  if result:defs[result]=(op,a,i,cur)
  if op=='OpConstant'and len(a)==2:
   try:const[result]=int(a[1],0)&0xffffffff
   except ValueError:pass
  if op=='OpConstantNull':const[result]=0
 return rows,defs,const,blocks

def require_no_external_writes_or_sync(rows,defs):
 """UBO bytes can alias another descriptor. Moving a load across a remote
 write/barrier is unsafe even when the UBO's own storage is readonly.
 Only statically rooted Output/Function/Private destinations are harmless.
 """
 def local_destination(pointer,seen=None):
  seen=set()if seen is None else seen
  if pointer in seen:return False
  seen.add(pointer);entry=defs.get(pointer)
  if not entry:return False
  op,a,_,_=entry
  if not a:return False
  typ=defs.get(a[0])
  if not typ or typ[0]!='OpTypePointer'or typ[1][0]not in ['Output','Function','Private']:return False
  if op=='OpVariable':return len(a)>=2 and a[1]==typ[1][0]
  if op in ['OpAccessChain','OpInBoundsAccessChain','OpCopyObject']and len(a)>=2:return local_destination(a[1],seen)
  return False # Phi/select/call/parameter/physical pointer: unknown provenance.
 imports={r:a[0]for _,r,op,a,_ in rows if op=='OpExtInstImport'and a}
 for _,_,op,a,_ in rows:
  if op=='OpExtInst':
   name=imports.get(a[1],'')if len(a)>2 else ''
   if name!='"GLSL.std.450"'and not name.startswith('"NonSemantic.'):raise ValueError('unknown external instruction effects outside movable Uniform load contract')
   if name=='"GLSL.std.450"'and a[2]in ['Modf','Frexp']and not local_destination(a[-1]):raise ValueError('external GLSL output pointer outside movable Uniform load contract')
  if op=='OpImageWrite'or op.startswith('OpAtomic')or 'Barrier'in op or 'InvocationInterlock'in op:
   raise ValueError('external write/atomic/synchronization outside movable Uniform load contract: '+op)
  if op=='OpStore':
   if len(a)!=2 or not local_destination(a[0]):raise ValueError('external or unknown memory store outside movable Uniform load contract')
  elif op in ['OpCopyMemory','OpCopyMemorySized']:
   required=3 if op=='OpCopyMemorySized'else 2
   if len(a)!=required or not local_destination(a[0]):raise ValueError('external or unknown memory copy outside movable Uniform load contract')
  elif 'Store'in op:raise ValueError('unsupported external storage operation outside movable Uniform load contract: '+op)


def transform(text):
 rows,defs,const,blocks=parse(text)
 eps=[a for _,_,op,a,_ in rows if op=='OpEntryPoint']
 if len(eps)!=1 or eps[0][0]!='Fragment':raise ValueError('one Fragment entry required')
 if any(op in ['OpDecorate','OpMemberDecorate'] and any(x in a for x in ['Volatile','Coherent']) for _,_,op,a,_ in rows):raise ValueError('volatile/coherent memory outside experiment contract')
 if any(op in ['OpDecorationGroup','OpGroupDecorate','OpGroupMemberDecorate']for _,_,op,_,_ in rows):raise ValueError('grouped decorations outside readonly proof contract')
 require_no_external_writes_or_sync(rows,defs)
 groups={}
 for result,(op,a,i,b)in defs.items():
  if op!='OpLoad'or len(a)!=2:continue # Any MemoryAccess operand is rejected.
  ptr=defs.get(a[1]);typ=defs.get(a[0])
  if not ptr or ptr[0]!='OpAccessChain'or len(ptr[1])<4:continue
  pa=ptr[1];pt=defs.get(pa[0]);base=defs.get(pa[1]);component=const.get(pa[-1])
  if not pt or pt[:2]!=('OpTypePointer',['Uniform',a[0]])or not base or base[0]!='OpVariable'or base[1][1]!='Uniform'or component not in range(4):continue
  # TypeFloat 32 is transported only by exact bitcast, not arithmetic conversion.
  if not typ or typ[:2]!=('OpTypeFloat',['32']):continue
  root_pointer=defs.get(base[1][0])
  if not root_pointer or root_pointer[0]!='OpTypePointer'or root_pointer[1][0]!='Uniform':continue
  root_block=root_pointer[1][1]
  if not any(dop=='OpDecorate'and da==[root_block,'Block']for _,_,dop,da,_ in rows):continue
  if any(dop=='OpDecorate'and da==[root_block,'BufferBlock']for _,_,dop,da,_ in rows):continue
  if any(dop=='OpDecorate'and da[0]in [result,a[1],pa[1]]and da[1]in ['Volatile','Coherent','RelaxedPrecision']for _,_,dop,da,_ in rows):continue
  groupkey=(tuple(pa[:-1]),a[0],b)
  groups.setdefault(groupkey,{}).setdefault(component,[]).append(result)
 candidates=[]
 for g,bycomp in groups.items():
  if set(bycomp)!=set(range(4))or any(len(x)!=1 for x in bycomp.values()):continue
  loads=[bycomp[c][0]for c in range(4)];symbols={v:z3.BitVec('W'+str(c),32)for c,v in enumerate(loads)}
  # The selector is exactly two unsigned bits. The expression evaluation below
  # rejects arbitrary inputs: no undefined/poison leaf can enter this proof.
  selectors=[r for r,(op,a,_,_)in defs.items()if op=='OpBitFieldUExtract'and len(a)==4 and const.get(a[3])==2 and const.get(a[2],99)<=30 and defs.get(a[0],('',[]))[:2]==('OpTypeInt',['32','0'])]
  for selector in selectors:
   C=z3.BitVec('C',32);memo={selector:C,**symbols};visiting=set();used=set()
   def ev(r):
    if r in memo:return memo[r]
    if r in const:return z3.BitVecVal(const[r],32)
    if r in visiting:raise ValueError('cyclic expression')
    visiting.add(r);op,a,_,block=defs[r];used.add(r)
    def v(n):return ev(a[n])
    if op in ['OpBitcast','OpCopyObject']:out=v(1)
    elif op=='OpIAdd':out=v(1)+v(2)
    elif op=='OpSNegate':out=-v(1)
    elif op=='OpBitwiseAnd':out=v(1)&v(2)
    elif op=='OpBitwiseOr':out=v(1)|v(2)
    elif op=='OpULessThan':out=z3.ULT(v(1),v(2))
    elif op=='OpUGreaterThanEqual':out=z3.UGE(v(1),v(2))
    elif op=='OpSelect':out=z3.If(v(1),v(2),v(3))
    elif op=='OpPhi'and len(a)==5:
     # Only canonical empty arm of one structured diamond is admitted: one
     # predecessor branches conditionally directly to merge or an empty arm.
     value0,label0,value1,label1=a[1:];out=None
     for source,arm,va,vb in [(label0,label1,value0,value1),(label1,label0,value1,value0)]:
      srows=[rows[k]for k in blocks[source]];arows=[rows[k]for k in blocks[arm]]
      if len(arows)!=2 or arows[1][2:]!=('OpBranch',[block],arm):continue
      term=srows[-1]
      if term[2]!='OpBranchConditional'or len(term[3])!=3:continue
      cond,yes,no=term[3]
      if {yes,no}!={arm,block}:continue
      out=z3.If(ev(cond),ev(vb),ev(va))if yes==arm else z3.If(ev(cond),ev(va),ev(vb));break
     if out is None:raise ValueError('noncanonical Phi control')
    else:raise ValueError('unsupported selector expression '+op)
    visiting.remove(r);memo[r]=out;return out
   for root,(op,a,ri,rb)in defs.items():
    if op!='OpBitwiseOr':continue
    try:
     used.clear();memo={selector:C,**symbols};visiting.clear();value=ev(root);graph=set(used)
     # Every word must actually participate; arbitrary payloads may be NaNs.
     if set(str(x)for x in z3.z3util.get_vars(value))!={'C','W0','W1','W2','W3'}:continue
     expected=z3.If(C==0,symbols[loads[0]],z3.If(C==1,symbols[loads[1]],z3.If(C==2,symbols[loads[2]],symbols[loads[3]])))
     solver=z3.Solver();solver.add(z3.ULE(C,3),value!=expected)
     if solver.check()!=z3.unsat:continue
     # Reject load/pointer use outside the proved selection DAG. Debug names and
     # decorations do not consume values but deleting them is DCE's job.
     proofids=graph|{root};pointers={defs[x][1][1]for x in loads}
     safe=True
     for _,rr,oo,aa,_ in rows:
      if oo in ['OpName','OpDecorate']:continue
      if any(x in aa for x in loads)and rr not in proofids:safe=False
      if any(x in aa for x in pointers)and rr not in loads:safe=False
     if not safe:continue
     if ri<=max(defs[x][2]for x in loads):continue
     candidates.append((g,loads,selector,root,graph,ri,rb))
    except (ValueError,KeyError,z3.Z3Exception):pass
 if len(candidates)!=1:raise ValueError('expected one uniquely proved four-word selector, got '+str(len(candidates)))
 g,loads,selector,root,graph,ri,oldblock=candidates[0]
 # Allocate fresh numeric IDs; original result root keeps its identity/decorations.
 nextid=max(int(x[1:])for x in defs if re.fullmatch(r'%\d+',x))+1
 def fresh():
  nonlocal nextid
  r='%'+str(nextid);nextid+=1;return r
 merge=fresh();low=fresh();high=fresh();lm=fresh();hm=fresh();labels=[fresh()for _ in range(4)];values=[]
 booltype=next((r for r,(op,a,_,_)in defs.items()if op=='OpTypeBool'),None)
 if booltype is None:raise ValueError('missing bool type')
 selector_type=defs[selector][1][0];k0=fresh();k2=fresh();constants_insert=[f'{k0} = OpConstant {selector_type} 0',f'{k2} = OpConstant {selector_type} 2'];outercond=fresh();lo_cond=fresh();hi_cond=fresh();lo_value=fresh();hi_value=fresh();root_type=defs[root][1][0];float_type=g[1]
 insert=[f'{outercond} = OpULessThan {booltype} {selector} {k2}',f'OpSelectionMerge {merge} DontFlatten',f'OpBranchConditional {outercond} {low} {high}',f'{low} = OpLabel',f'{lo_cond} = OpIEqual {booltype} {selector} {k0}',f'OpSelectionMerge {lm} DontFlatten',f'OpBranchConditional {lo_cond} {labels[0]} {labels[1]}']
 def branch(c):
  label=labels[c];pointer=fresh();loaded=fresh();cast=fresh();values.append(cast);access=defs[defs[loads[c]][1][1]][1]
  return [f'{label} = OpLabel',f'{pointer} = OpAccessChain '+' '.join(access),f'{loaded} = OpLoad {float_type} {pointer}',f'{cast} = OpBitcast {root_type} {loaded}']
 for c in [0,1]:insert.extend(branch(c)+[f'OpBranch {lm}'])
 insert.extend([f'{lm} = OpLabel',f'{lo_value} = OpPhi {root_type} {values[0]} {labels[0]} {values[1]} {labels[1]}',f'OpBranch {merge}',f'{high} = OpLabel',f'{hi_cond} = OpIEqual {booltype} {selector} {k2}',f'OpSelectionMerge {hm} DontFlatten',f'OpBranchConditional {hi_cond} {labels[2]} {labels[3]}'])
 for c in [2,3]:insert.extend(branch(c)+[f'OpBranch {hm}'])
 insert.extend([f'{hm} = OpLabel',f'{hi_value} = OpPhi {root_type} {values[2]} {labels[2]} {values[3]} {labels[3]}',f'OpBranch {merge}',f'{merge} = OpLabel',f'{root} = OpPhi {root_type} {lo_value} {lm} {hi_value} {hm}'])
 inserted_constants=False
 output=[]
 for i,(line,r,op,a,b)in enumerate(rows):
  if op=='OpFunction'and not inserted_constants:output.extend(constants_insert);inserted_constants=True
  if i==ri:output.extend(insert);continue
  # Splitting oldblock moves its original terminator to merge. Its successors'
  # Phi incoming predecessor must follow that terminator, not the oldblock.
  if op=='OpPhi':
   a=list(a)
   for j in range(2,len(a),2):
    if a[j]==oldblock:a[j]=merge
   line=f'{r} = OpPhi '+' '.join(a)
  output.append(line)
 proof={'solver':z3.get_version_string(),'counterexample':'UNSAT','selector':selector,'selector_domain':'OpBitFieldUExtract width=2, all uint32 payloads','original_loads':loads,'root':root,'old_block':oldblock,'new_merge':merge,'constant_component_branches':4,'nested_branchconditional_headers':3,'all_selection_headers_DontFlatten':True,'original_vector_prefix':list(g[0]),'no_volatile_or_expanded_addresses':True,'module_external_writes_atomics_and_sync_absent':True,'allowed_store_roots':['Output','Function','Private'],'bitcast_preserved':True,'scope':'Pure Uniform load/mask pattern. No source/descriptor/layout/range or nonselected word mutation; non-uniform branches may cost all4 wave paths.'}
 return '\n'.join(output)+'\n',proof

def compile_candidate(spv,output,tools):
 tool=lambda name:str(tools[name])if isinstance(tools,dict)else str(tools/name)
 code=Path(spv).read_bytes();dis=subprocess.run([tool('spirv-dis'),'--raw-id','-o','-','-'],input=code,check=True,capture_output=True).stdout.decode();text,proof=transform(dis);output=Path(output);output.with_suffix('.before-dce.spvasm').write_text(text)
 binary=subprocess.run([tool('spirv-as'),'--preserve-numeric-ids','--target-env','spv1.3','-o','-','-'],input=text.encode(),check=True,capture_output=True).stdout
 subprocess.run([tool('spirv-val'),'--target-env','vulkan1.1','-'],input=binary,check=True,capture_output=True)
 # Only dead-code cleanup: no branch flatten/if conversion is requested.
 final=subprocess.run([tool('spirv-opt'),'--eliminate-dead-code-aggressive','-o','-','-'],input=binary,check=True,capture_output=True).stdout
 subprocess.run([tool('spirv-val'),'--target-env','vulkan1.1','-'],input=final,check=True,capture_output=True)
 output.write_bytes(final);asm=subprocess.run([tool('spirv-dis'),'--raw-id','-o','-','-'],input=final,check=True,capture_output=True).stdout.decode();output.with_suffix('.spvasm').write_text(asm)
 # Ensure all original group loads disappeared; generic DCE must not leave the
 # unconditional vec4 active alongside candidate branch loads.
 _,dd,_,_=parse(asm)
 if any(x in dd for x in proof['original_loads']):raise ValueError('original unconditional loads survived DCE')
 proof.update(input_sha256=hashlib.sha256(code).hexdigest(),output_sha256=hashlib.sha256(final).hexdigest(),output_bytes=len(final),original_unconditional_loads_removed=True)
 return proof
