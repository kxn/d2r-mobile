"""Optional exact per-draw Uniform specialization, never a fixed-frame value.

SPIRV-Tools supplies normalized SSA, validation and constant folding. This
adapter only supplies the missing Uniform-to-SpecConstant representation and
an auditable one-word recipe. The runtime must prove CPU visibility, include
all value bits in both PSO keys, and otherwise use the unchanged base module.
No shader names, game keys, scene conditions or captured values enter policy.
"""
from pathlib import Path
import hashlib,struct,subprocess,tempfile
MAGIC=0x32505344
SPEC_ID=0

def transform(code, eligible_bindings, minimum_weight=128):
    with tempfile.TemporaryDirectory(prefix='dmsh_uniform_spec_')as temp:
        root=Path(temp);source=root/'source.spv';normal=root/'normal.spv';source.write_bytes(code)
        subprocess.run(['spirv-opt','--inline-entry-points-exhaustive','--ssa-rewrite','--simplify-instructions','--eliminate-dead-code-aggressive',str(source),'-o',str(normal)],check=True,capture_output=True)
        text=subprocess.check_output(['spirv-dis','--raw-id',str(normal)],text=True)
        lines=text.splitlines();defs={};instructions=[];blocks={};bb=None;decorations={};members={}
        for index,line in enumerate(lines):
            t=line.split(';',1)[0].split();rid=None
            if not t:continue
            if len(t)>2 and t[1]=='=':rid=t[0];t=t[2:]
            op,a=t[0],t[1:]
            if op=='OpLabel':bb=rid;blocks[bb]=[]
            row=(rid,op,a,bb,index);instructions.append(row)
            if rid:defs[rid]=row
            if bb:blocks[bb].append(row)
            if op=='OpFunctionEnd':bb=None
            if op=='OpDecorate'and len(a)==3:decorations[(a[0],a[1])]=a[2]
            if op=='OpMemberDecorate'and len(a)==4:members[(a[0],a[1],a[2])]=a[3]
        if any(op=='OpDecorate'and a[1]=='SpecId'for _,op,a,_,_ in instructions):return None,{'skipped':'existing specialization constants'}
        constants={rid:int(a[1])for rid,op,a,_,_ in instructions if op=='OpConstant'and defs[a[0]][1]=='OpTypeInt'}
        roots={}
        for rid,op,a,_,_ in instructions:
            if op!='OpVariable'or a[1]!='Uniform'or decorations.get((rid,'DescriptorSet'))!='1':continue
            binding=int(decorations.get((rid,'Binding'),'-1'))
            if binding not in eligible_bindings:continue
            ptr=defs[a[0]];st=defs.get(ptr[2][1])if ptr[1]=='OpTypePointer'else None
            if not st or st[1]!='OpTypeStruct'or len(st[2])!=1 or members.get((st[0],'0','Offset'))!='0':continue
            ar=defs[st[2][0]]
            if ar[1]!='OpTypeArray'or constants.get(ar[2][1])!=4096 or decorations.get((ar[0],'ArrayStride'))!='16':continue
            vec=defs[ar[2][0]]
            if vec[1]!='OpTypeVector'or vec[2][1]!='4':continue
            scalar=defs[vec[2][0]]
            if scalar[1]!='OpTypeFloat'or scalar[2]!=['32']:continue
            roots[rid]=(binding,scalar[0])
        loads={}
        for rid,op,a,_,_ in instructions:
            if op!='OpLoad'or len(a)!=2:continue
            p=defs.get(a[1]);pa=p[2]if p else[]
            if not p or p[1]not in ['OpAccessChain','OpInBoundsAccessChain']or len(pa)!=5 or pa[1]not in roots or any(x not in constants for x in pa[2:]):continue
            binding,scalar=roots[pa[1]]
            if a[0]!=scalar or constants[pa[2]]!=0 or not 0<=constants[pa[3]]<4096 or not 0<=constants[pa[4]]<4:continue
            loads[rid]=(binding,constants[pa[3]]*16+constants[pa[4]]*4,scalar)
        memo={}
        def dependencies(x,seen=None):
            if x in memo:return memo[x]
            seen=set()if seen is None else set(seen)
            if x in seen:return None
            seen.add(x);d=defs.get(x)
            if not d:return None
            _,op,a,_,_=d
            if op in ['OpConstant','OpConstantTrue','OpConstantFalse','OpConstantNull']:return set()
            if x in loads:return {loads[x]}
            if op in ['OpLoad','OpPhi','OpFunctionCall','OpVariable','OpUndef','OpLabel','OpExtInstImport','OpFunction']or op.startswith(('OpType','OpImage','OpGroup','OpDP','OpFwidth','OpAtomic')):return None
            values=[v for v in(a[3:]if op=='OpExtInst'else a[1:])if v.startswith('%')]
            if not values:return None
            result=set()
            for v in values:
                source=dependencies(v,seen)
                if source is None:memo[x]=None;return None
                result|=source
            memo[x]=result;return result
        def successors(b):
            term=next((r for r in reversed(blocks[b])if r[1]not in ['OpFunctionEnd','OpLine','OpNoLine']),None)
            if not term:return []
            _,op,a,_,_=term
            return a[:1]if op=='OpBranch'else a[1:3]if op=='OpBranchConditional'else[a[1],*a[3::2]]if op=='OpSwitch'else[]
        scores={}
        for _,op,a,b,_ in instructions:
            if op!='OpBranchConditional':continue
            words=dependencies(a[0])
            if words is None or len(words)!=1:continue
            merge=next((r[2][0]for r in blocks[b]if r[1]in ['OpSelectionMerge','OpLoopMerge']),None);visited=set();todo=a[1:3]
            while todo:
                node=todo.pop()
                if node==merge or node in visited or node not in blocks:continue
                visited.add(node);todo.extend(successors(node))
            word=next(iter(words));scores[word]=scores.get(word,0)+sum(len(blocks[n])for n in visited)
        if not scores:return None,{'skipped':'no one-word Uniform branch selector'}
        word=max(scores,key=lambda v:(scores[v],-v[0],-v[1]));binding,offset,scalar=word
        if scores[word]<minimum_weight:return None,{'skipped':'branch region below threshold','weight':scores[word]}
        # Integer-observed selector words must remain integer bit patterns:
        # float SpecConstants can canonicalize NaN/denormal flag encodings.
        uses={}
        for rid,op,a,b,index in instructions:
            if b is not None:
                for operand in a[1 if rid else 0:]:
                    if operand.startswith('%'):uses.setdefault(operand,[]).append((rid,op,a))
        def raw_observation(value,seen=None):
            seen=set()if seen is None else set(seen)
            if value in seen:return False
            seen.add(value)
            for rid,op,a in uses.get(value,[]):
                if op=='OpBitcast'and defs[a[0]][1]=='OpTypeInt'and defs[a[0]][2][0]=='32':continue
                if op=='OpCopyObject'and raw_observation(rid,seen):continue
                return False
            return True
        integer_only=all(raw_observation(rid)for rid in loads if loads[rid]==word)
        bound=struct.unpack_from('<I',normal.read_bytes(),12)[0]
        spec='%'+str(bound);bound+=1;changed=0
        uint=next((rid for rid,op,a,_,_ in instructions if op=='OpTypeInt'and a==['32','0']),None)
        new_uint=uint is None
        if new_uint:uint='%'+str(bound)

        for rid,op,a,_,index in instructions:
            if op=='OpLoad'and loads.get(rid)==word:
                lines[index]=f'{rid} = OpBitcast {scalar} {spec}';changed+=1
        first_function=next(i for i,l in enumerate(lines)if '= OpFunction 'in l)
        lines.insert(first_function,f'{spec} = OpSpecConstant {uint} 0')
        first_type=next(i for i,l in enumerate(lines)if '= OpType'in l)
        if new_uint:lines.insert(first_type,f'{uint} = OpTypeInt 32 0')
        lines.insert(first_type,f'OpDecorate {spec} SpecId {SPEC_ID}')
        asm=root/'specialized.spvasm';binary=root/'specialized.spv';asm.write_text('\n'.join(lines)+'\n')
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(binary)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(binary)],check=True,capture_output=True)
        spv=binary.read_bytes();reg=eligible_bindings[binding]
        header=struct.pack('<7I',MAGIC,1|(256 if integer_only else 0),binding,reg,offset,SPEC_ID,len(spv)//4)+hashlib.sha256(code).digest()
        return header+spv,{'binding':binding,'reg':reg,'offset':offset,'spec_id':SPEC_ID,'scalar_loads':changed,'integer_only':integer_only,'branch_weight':scores[word],'base_sha256':hashlib.sha256(code).hexdigest(),'specialization_sha256':hashlib.sha256(header+spv).hexdigest()}
