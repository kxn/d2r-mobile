#!/usr/bin/env python3
"""Conservative, content-bound pixel-local fetch candidates, never a draw proof.

Only official SPIR-V normalization/validation is used. Every sample/use of a
candidate descriptor in every function and branch must be understood. Runtime
must additionally prove the listed geometry, sampler and attachment conditions.
No module is replaced by emit(); input_variant() is an explicit experiment.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from dmsh_uniform_predicates import _parse

VERSION = 1
NORMALIZE = ['--inline-entry-points-exhaustive', '--ssa-rewrite',
             '--simplify-instructions', '--eliminate-dead-code-aggressive']
REQUIREMENTS = [
    'exact source attachment/view identity, compatible format and identity swizzle',
    'one sample; 2D non-array view; exact mip and layer; source and destination extents equal',
    'full viewport/scissor; no rotation; vertex position w=1; finite per-vertex UV equals normalized framebuffer position',
    'UV proof for every actual index/instance; no GPU-pending or unreadable geometry/affine coefficients',
    'smooth interpolation; no sample shading; full-precision coordinates and exact framebuffer texel center coordinates',
    'CPU geometry/coefficients remain stable under the original NVN synchronization contract',
    'normalized sampler; nearest or linear min/mag; zero bias/minLod; no anisotropy/compare/reduction',
    'implicit sample: derivatives select effective LOD 0 (including sampler/view clamps); explicit sample: exactly LOD 0',
    'no feedback conflict or intervening source write; preserve all other readers, attachment loads/stores, queries and dependencies',
]


def sha(data): return hashlib.sha256(data).hexdigest()


class Module:
    def __init__(self, code):
        with tempfile.TemporaryDirectory(prefix='dmsh_pixel_local_') as tmp:
            src=Path(tmp)/'in.spv'; out=Path(tmp)/'ssa.spv'; src.write_bytes(code)
            subprocess.run(['spirv-val','--target-env','vulkan1.1',str(src)],check=True,capture_output=True)
            subprocess.run(['spirv-opt',*NORMALIZE,str(src),'-o',str(out)],check=True,capture_output=True)
            subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out)],check=True,capture_output=True)
            self.code=out.read_bytes()
            self.text=subprocess.check_output(['spirv-dis','--raw-id',str(out)],text=True)
        self.defs,self.rows,self.decs,self.members=_parse(self.text)
        self.constants={rid:int(a[1],0) for rid,op,a,_ in self.rows
                        if op=='OpConstant' and self.defs[a[0]][1]=='OpTypeInt'}
        self.expressions={}
        for rid,op,a,_ in self.rows:
            if op.startswith('OpType') and 'Pointer' in op and op!='OpTypePointer':
                raise ValueError('unsupported pointer type '+op)
            if op=='OpCapability' and a[0] in ('VariablePointers','VariablePointersStorageBuffer'):
                raise ValueError('variable pointer capability')
            if op in ('OpDecorationGroup','OpGroupDecorate','OpGroupMemberDecorate','OpDecorateId','OpDecorateString','OpMemberDecorateString'):
                raise ValueError('unexpanded decorations')
            if rid and a and self.defs.get(a[0],(None,None))[1]=='OpTypePointer' and op not in (
                    'OpVariable','OpAccessChain','OpInBoundsAccessChain'):
                raise ValueError('unsupported pointer propagation '+op)
        entries=[a for _,op,a,_ in self.rows if op=='OpEntryPoint']
        if len(entries)!=1: raise ValueError('entry point count')
        self.stage=entries[0][0]

    def row(self,rid): return self.defs[rid][1:3]
    def deco(self,rid,key): return self.decs.get((rid,key))
    def number(self,rid,key):
        value=self.deco(rid,key)
        if not isinstance(value,list) or len(value)!=1: raise ValueError('missing '+key)
        return int(value[0])
    def pointer(self,rid):
        op,a=self.row(rid)
        if op=='OpVariable': return rid,[]
        if op not in ('OpAccessChain','OpInBoundsAccessChain'): raise ValueError('pointer expression')
        root,path=self.pointer(a[1])
        return root,path+[self.constants[x] for x in a[2:]]
    def interface(self,root,path):
        op,a=self.row(root); typ=self.row(a[0])[1][1]
        builtin=self.deco(root,'BuiltIn')
        if self.row(typ)[0]=='OpTypeStruct':
            if not path: raise ValueError('whole interface struct')
            member=path[0];builtin=self.members.get((typ,str(member),'BuiltIn'));path=path[1:]
            if not builtin: raise ValueError('unsupported interface member')
        if builtin:
            return ('builtin',builtin[0] if isinstance(builtin,list) else builtin,tuple(path))
        for q in ('Flat','NoPerspective','Centroid','Sample','Patch','Component','RelaxedPrecision'):
            if self.deco(root,q) is not None: raise ValueError('interpolation/component qualifier')
        return ('varying',self.number(root,'Location'),tuple(path))
    def uniform(self,root,path):
        _,a=self.row(root);typ=self.row(a[0])[1][1];offset=0
        for index in path:
            op,args=self.row(typ)
            if op=='OpTypeStruct':
                if index<0 or index>=len(args): raise ValueError('uniform struct bounds')
                off=self.members.get((typ,str(index),'Offset'))
                if off is None: raise ValueError('uniform layout')
                if any((typ,str(index),q) in self.members for q in ('Volatile','Coherent','Aliased','RowMajor','MatrixStride')):
                    raise ValueError('qualified uniform member')
                offset+=int(off);typ=args[index]
            elif op=='OpTypeArray':
                if index<0 or index>=self.constants[args[1]]: raise ValueError('uniform array bounds')
                stride=self.deco(typ,'ArrayStride')
                if not stride: raise ValueError('uniform stride')
                offset+=index*int(stride[0]);typ=args[0]
            elif op=='OpTypeVector':
                if index<0 or index>=int(args[1]) or self.row(args[0])!=('OpTypeFloat',['32']):
                    raise ValueError('uniform scalar type/bounds')
                offset+=index*4;typ=args[0]
            else: raise ValueError('uniform indexed type')
        for q in ('Volatile','Coherent','Aliased','RelaxedPrecision'):
            if self.deco(root,q) is not None: raise ValueError('qualified uniform')
        if self.row(typ)[0]=='OpTypeVector':
            element,count=self.row(typ)[1]
            if self.row(element)!=('OpTypeFloat',['32']):raise ValueError('uniform vector type')
            return ('vector',*(self.uniform(root,path+[c]) for c in range(int(count))))
        if self.row(typ)!=('OpTypeFloat',['32']): raise ValueError('uniform result type')
        return ('uniform',self.number(root,'DescriptorSet'),self.number(root,'Binding'),offset)
    def expr(self,rid,depth=0):
        if rid in self.expressions: return self.expressions[rid]
        if any(self.deco(rid,q) is not None for q in ('RelaxedPrecision','FPRoundingMode','FPFastMathMode')):
            raise ValueError('relaxed coordinate/affine precision')
        if depth>48: raise ValueError('expression depth')
        op,a=self.row(rid); e=lambda x:self.expr(x,depth+1)
        if op=='OpConstant' and self.row(a[0])==('OpTypeFloat',['32']): result=('constant',float(a[1]))
        elif op in ('OpConstantComposite','OpCompositeConstruct'):
            components=[]
            for x in a[1:]:
                value=e(x);components.extend(value[1:] if value[0]=='vector' else [value])
            result=('vector',*components)
        elif op=='OpLoad' and len(a)==2:
            root,path=self.pointer(a[1]);storage=self.row(root)[1][1]
            if storage=='Input':
                base=self.interface(root,path)
                if base[0]!='varying': raise ValueError('nonvarying load')
                typ=self.row(a[0])
                if not path and typ[0]=='OpTypeVector' and self.row(typ[1][0])==('OpTypeFloat',['32']):
                    result=('vector',*(('input',base[1],c) for c in range(int(typ[1][1]))))
                elif len(path)==1 and self.row(a[0])==('OpTypeFloat',['32']): result=('input',base[1],path[0])
                else: raise ValueError('input layout/type')
            elif storage=='Uniform': result=self.uniform(root,path)
            else: raise ValueError('non-interface expression')
        elif op=='OpCompositeExtract':
            value=e(a[1]);indices=[int(x) for x in a[2:]]
            for index in indices:
                if value[0]!='vector': raise ValueError('nonvector extract')
                value=value[index+1]
            result=value
        elif op=='OpVectorShuffle':
            left=e(a[1]);right=e(a[2])
            if left[0]!='vector' or right[0]!='vector': raise ValueError('shuffle inputs')
            values=left[1:]+right[1:];result=('vector',*(values[int(x)] for x in a[3:]))
        elif op=='OpCopyObject': result=e(a[1])
        elif op=='OpExtInst' and a[2]=='Fma' and self.row(a[1])==('OpExtInstImport',['"GLSL.std.450"']):
            values=[e(x) for x in a[3:]]
            if values[0][0]=='vector':
                if any(v[0]!='vector' or len(v)!=len(values[0]) for v in values): raise ValueError('fma shape')
                result=('vector',*(('fma',*(v[c] for v in values)) for c in range(1,len(values[0]))))
            else: result=('fma',*values)
        elif op=='OpSelect':
            condop,cond=self.row(a[1]);yes=e(a[2]);no=e(a[3])
            if condop!='OpIsNan' or e(cond[1])!=no or no[0]!='vector' or yes[0]!='vector' or any(x!=('constant',0.0) for x in yes[1:]) or len(yes)!=len(no):
                raise ValueError('not exact NaN sanitation')
            result=no  # Runtime proves finite coordinates, so this select is identity.
        else: raise ValueError('unsupported coordinate/value '+op)
        self.expressions[rid]=result;return result


def _base(code,m):
    return {'schema':VERSION,'source_sha256':sha(code),'normalized_sha256':sha(m.code),
            'normalization':NORMALIZE,'stage':m.stage,'runtime_requirements':REQUIREMENTS,
            'status':'candidate-only; no pass or draw authorization'}


def analyze_fragment(code):
    m=Module(code); report=_base(code,m)
    if m.stage!='Fragment': raise ValueError('not fragment')
    descriptors={}
    for rid,op,a,_ in m.rows:
        if op!='OpVariable' or a[1]!='UniformConstant': continue
        try:
            sampled=m.row(m.row(a[0])[1][1]);image=m.row(sampled[1][0])
            if sampled[0]!='OpTypeSampledImage': continue
            descriptors[rid]={'set':m.number(rid,'DescriptorSet'),'binding':m.number(rid,'Binding'),
                'eligible':True,'samples':[],'reasons':[]}
            if image[0]!='OpTypeImage' or image[1]!=[image[1][0],'2D','0','0','0','1','Unknown'] or m.row(image[1][0])!=('OpTypeFloat',['32']):
                raise ValueError('not float32 nondepth nonarray single-sample 2D')
        except (ValueError,KeyError,IndexError) as err:
            if rid in descriptors: descriptors[rid]['eligible']=False;descriptors[rid]['reasons'].append(str(err))
    loaded={}
    def reject(root,reason):
        d=descriptors[root];d['eligible']=False
        if reason not in d['reasons']:d['reasons'].append(reason)
    for rid,op,a,_ in m.rows:
        roots=[x for x in a if x in descriptors]
        for root in roots:
            if op=='OpLoad' and len(a)==2 and a[1]==root: loaded[rid]=root
            elif op not in ('OpDecorate','OpName','OpEntryPoint'): reject(root,'descriptor escape/use '+op)
    for rid,op,a,_ in m.rows:
        for value in set(a)&loaded.keys():
            root=loaded[value]
            try:
                if len(a)<3 or a[1]!=value or op not in ('OpImageSampleImplicitLod','OpImageSampleExplicitLod'):
                    raise ValueError('mixed/unsupported sampled-image use '+op)
                result_type=m.row(a[0])
                if result_type[0]!='OpTypeVector' or result_type[1][1]!='4' or m.row(result_type[1][0])!=('OpTypeFloat',['32']):
                    raise ValueError('sample result type')
                lod='implicit-zero-required'
                if op=='OpImageSampleImplicitLod':
                    if a[3:] not in ([],['None']): raise ValueError('implicit image operands')
                else:
                    if len(a)!=5 or a[3]!='Lod' or m.expr(a[4])!=('constant',0.0): raise ValueError('nonzero/unknown/extra explicit LOD')
                    lod='explicit-zero'
                uv=m.expr(a[2])
                if len(uv)!=3 or uv[0]!='vector' or uv[1][0]!='input' or uv[2]!=( 'input',uv[1][1],1) or uv[1][2]!=0:
                    raise ValueError('not direct varying xy')
                descriptors[root]['samples'].append({'result_id':rid,'varying_location':uv[1][1],'lod':lod})
            except (ValueError,KeyError,IndexError) as err: reject(root,str(err))
    for d in descriptors.values():
        if not d['samples']:d['eligible']=False;d['reasons'].append('no eligible samples')
        if len(d['samples'])>128:d['eligible']=False;d['reasons'].append('sample count limit')
    report['bindings']=sorted(descriptors.values(),key=lambda d:(d['set'],d['binding']))
    return report


def analyze_vertex(code):
    m=Module(code); report=_base(code,m)
    if m.stage!='Vertex': raise ValueError('not vertex')
    # A closed straight-line transfer recipe. All output writes participate in
    # final-value resolution; branches, calls, unknown pointer writes reject.
    if sum(op=='OpFunction' for _,op,_,_ in m.rows)!=1:raise ValueError('vertex function closure')
    if any(op in ('OpBranch','OpBranchConditional','OpSwitch','OpFunctionCall') for _,op,_,_ in m.rows):
        raise ValueError('vertex control flow')
    outputs={}
    for _,op,a,_ in m.rows:
        if op!='OpStore':continue
        if len(a)!=2:raise ValueError('store memory operands')
        root,path=m.pointer(a[0]);storage=m.row(root)[1][1]
        if storage=='Function':continue
        if storage!='Output':raise ValueError('vertex side effect')
        where=m.interface(root,path);value=m.expr(a[1])
        if value[0]=='vector':
            if where[2]:raise ValueError('nested output vector')
            for component,scalar in enumerate(value[1:]):outputs[where[:2]+((component,),)]=scalar
        else:outputs[where]=value
    position=[outputs.get(('builtin','Position',(c,))) for c in range(4)]
    if not position[0] or position[0][0]!='input' or position[:3]!=[('input',position[0][1],c) for c in range(3)] or position[3]!=('constant',1.0):
        raise ValueError('position is not same attribute xyz/w1')
    report['position']={'attribute':position[0][1],'w':1}
    recipes=[]
    for location in sorted({key[1] for key in outputs if key[0]=='varying'}):
        xy=[outputs.get(('varying',location,(c,))) for c in (0,1)]
        if xy[0] and xy[0][0]=='input' and xy==[('input',xy[0][1],c) for c in (0,1)]:
            recipes.append({'location':location,'attribute':xy[0][1],'mode':'direct'})
        elif all(v and v[0]=='fma' and v[1][0]=='input' and all(x[0]=='uniform' for x in v[2:]) for v in xy) and xy[0][1][1]==xy[1][1][1] and [v[1][2] for v in xy]==[0,1]:
            recipes.append({'location':location,'attribute':xy[0][1][1],'mode':'fma',
                            'scale':[v[2][1:] for v in xy],'bias':[v[3][1:] for v in xy]})
    report['uv_recipes']=recipes
    return report


def analyze(code):
    try:
        m=Module(code)
        return analyze_fragment(code) if m.stage=='Fragment' else analyze_vertex(code) if m.stage=='Vertex' else {'schema':VERSION,'source_sha256':sha(code),'stage':m.stage,'rejected':'stage'}
    except (ValueError,KeyError,IndexError,subprocess.CalledProcessError) as error:
        return {'schema':VERSION,'source_sha256':sha(code),'rejected':str(error)}


def input_variant(code, mappings):
    """Explicit experiment: {(set,binding):input_index}; no runtime activation.

    Preserve all other shader operations and resource bindings. The separate
    module requires a different descriptor schema and a compatible subpass;
    it is unusable by the original runtime/PSO without that explicit setup.
    """
    m=Module(code);proof=analyze_fragment(code)
    eligible={(d['set'],d['binding']):d for d in proof['bindings'] if d['eligible']}
    if not mappings or any(k not in eligible for k in mappings): raise ValueError('unproven selected binding')
    if any(not isinstance(v,int) or not 0<=v<8 for v in mappings.values()) or len(set(mappings.values()))!=len(mappings):
        raise ValueError('input attachment indices')
    roots={}
    for rid,op,a,_ in m.rows:
        if op=='OpVariable' and a[1]=='UniformConstant':
            try:key=(m.number(rid,'DescriptorSet'),m.number(rid,'Binding'))
            except ValueError:continue
            if key in mappings:roots[rid]=key
    if len(roots)!=len(mappings):raise ValueError('ambiguous selected descriptors')
    loaded={rid for rid,op,a,_ in m.rows if op=='OpLoad' and a[1] in roots}
    sample_ids={s['result_id'] for k in mappings for s in eligible[k]['samples']}
    float_id=next(r for r,op,a,_ in m.rows if op=='OpTypeFloat' and a==['32'])
    # Reuse existing non-aggregate types; SPIR-V forbids duplicate declarations.
    additions=[]
    def declaration(name,op,args):
        found=next((rid for rid,o,a,_ in m.rows if o==op and a==args),None)
        if found:return found
        additions.append(f"%pl_{name} = {op} "+' '.join(args));return '%pl_'+name
    image=declaration('image','OpTypeImage',[float_id,'SubpassData','0','0','0','2','Unknown'])
    pointer=declaration('pointer','OpTypePointer',['UniformConstant',image])
    image_additions='\n'.join(additions);additions=[]
    integer=declaration('int','OpTypeInt',['32','1'])
    vector=declaration('v2int','OpTypeVector',[integer,'2'])
    zero=declaration('zero','OpConstant',[integer,'0'])
    coord=declaration('coord','OpConstantComposite',[vector,zero,zero])
    extra='\n'.join(additions)
    lines=m.text.splitlines();inserted=False;cap=False
    for rid,op,a,index in m.rows:
        if op=='OpCapability' and not cap:
            lines[index]+='\nOpCapability InputAttachment';cap=True
        if op=='OpDecorate' and a[0] in roots and a[1]=='Binding':
            lines[index]+=f"\nOpDecorate {a[0]} InputAttachmentIndex {mappings[roots[a[0]]]}"
        if op=='OpFunction' and extra:
            lines[index]=extra+'\n'+lines[index];extra=''
        if rid in roots:
            lines[index]=f"{rid} = OpVariable {pointer} UniformConstant"
            if not inserted:lines[index]=image_additions+'\n'+lines[index];inserted=True
        elif rid in loaded:lines[index]=f"{rid} = OpLoad {image} {a[1]}"
        elif rid in sample_ids:lines[index]=f"{rid} = OpImageRead {a[0]} {a[1]} {coord}"
    with tempfile.TemporaryDirectory(prefix='dmsh_pixel_input_') as tmp:
        asm=Path(tmp)/'input.spvasm';out=Path(tmp)/'input.spv';asm.write_text('\n'.join(lines))
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(out)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out)],check=True,capture_output=True)
        subprocess.run(['spirv-opt','--eliminate-dead-code-aggressive',str(out),'-o',str(out)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out)],check=True,capture_output=True)
        result=out.read_bytes()
    return result, {'schema':VERSION,'source_sha256':sha(code),'variant_sha256':sha(result),
                    'inputs':[{'set':k[0],'binding':k[1],'input_index':v,'descriptor_type':'VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT'} for k,v in sorted(mappings.items())],
                    'replaced_samples':len(sample_ids),'runtime_requirements':REQUIREMENTS,
                    'status':'offline experiment; original module and default runtime unchanged'}


def emit(root,key,reflection=None):
    """Optional sidecar, separately hash-bound to every actual module artifact."""
    root=Path(root);blobs={s:(root/(key+'.'+s)).read_bytes() for s in ('spv','meta','spec','pred','tssbo') if (root/(key+'.'+s)).is_file()}
    report={'schema':VERSION,'closure_verified':True,'consumer_rule':'exact module SHA only; do not reuse base proof for alternatives','artifacts':{s:sha(b) for s,b in blobs.items()},'modules':{'base':analyze(blobs['spv'])}}
    try:
        if 'spec' in blobs:
            from dmsh_uniform_predicates import _old_recipe
            _old_recipe(blobs['spv'],blobs['spec']);report['modules']['spec']=analyze(blobs['spec'][60:])
        if 'pred' in blobs:
            from dmsh_uniform_predicates import unpack
            report['modules']['pred']=analyze(unpack(blobs['pred'],blobs['spv'],blobs['spec'])[0])
        if 'tssbo' in blobs:
            from dmsh_texel_ssbo import unpack_pair
            if not (reflection or {}).get('texel_storage_alternative'):raise ValueError('missing paired-module proof')
            alt,spec_alt,_=unpack_pair(blobs['tssbo'],blobs['spv'],blobs.get('spec'),(reflection or {}).get('texel_storage_alternative'))
            report['modules']['tssbo']=analyze(alt)
            if spec_alt:report['modules']['tssbo_spec']=analyze(spec_alt)
    except (ValueError,KeyError,subprocess.CalledProcessError) as error:
        report['closure_verified']=False;report['closure_rejected']=str(error)  # Never authorize an unverified optional module.
    path=root/(key+'.plocal.json');data=(json.dumps(report,sort_keys=True,indent=1)+'\n').encode();path.write_bytes(data)
    return {'sha256':sha(data),'schema':VERSION}


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('keys',nargs='*');p.add_argument('--input-attachment',action='append',default=[],metavar='SET:BINDING:INDEX')
    a=p.parse_args()
    for key in a.keys or sorted(v.stem for v in a.root.glob('*.spv')):
        refl=a.root/(key+'.json');print(key,emit(a.root,key,json.loads(refl.read_text()) if refl.is_file() else None))
        if a.input_attachment:
            entries=[tuple(map(int,v.split(':'))) for v in a.input_attachment]
            if any(len(v)!=3 for v in entries) or len({v[:2] for v in entries})!=len(entries):p.error('unique SET:BINDING:INDEX required')
            code,proof=input_variant((a.root/(key+'.spv')).read_bytes(),{(s,b):i for s,b,i in entries})
            (a.root/(key+'.plocal-input.spv')).write_bytes(code)
            (a.root/(key+'.plocal-input.json')).write_text(json.dumps(proof,sort_keys=True,indent=1)+'\n')
