"""Optional all-variant byte spans; official SPIR-V tools own validation."""
import hashlib
import importlib.util
import json
import struct
import subprocess
import unittest
from pathlib import Path
from unittest import mock
import test_driver_keys as old_tests
import dmsh_driver_keys as policy


class DriverSpans(unittest.TestCase):
    setUp = old_tests.DriverKeys.setUp
    tearDown = old_tests.DriverKeys.tearDown
    code = old_tests.DriverKeys.code

    def blobs(self, code, slot=2, stage=5, descriptor_set=2):
        return {'meta':struct.pack('<8I',0x314b5644,stage,descriptor_set,1,0,1,1,1)+
                struct.pack('<8i',1,1,slot,0,0,1,0,0), 'spv':code}

    def span(self, code, slot=2):
        key, proof, span, report = policy.build_with_spans(self.blobs(code,slot))
        self.assertEqual(len(span),96)
        self.assertEqual(struct.unpack_from('<4I',span),(policy.SPAN_MAGIC,1,5,2))
        self.assertEqual(span[16:48],key[16:48])
        self.assertEqual(hashlib.sha256(span[:64]).digest(),span[64:])
        return key, proof, span, report[str(slot)]

    def test_branch_high_zero_tail_is_still_read(self):
        _,_,_,p=self.span(self.code('if(t[1].x==0u)value=t[256];else value=t[2]'))
        self.assertEqual(p['max_read_end'],4112)

    def test_zero65520_is_not_empty_or_small(self):
        _,key,_,p=self.span(self.code('value=t[4095]'))
        self.assertEqual(key['2']['words'],[])
        self.assertEqual(p['max_read_end'],65536)

    def test_dense_key_fallback_keeps_independent_byte_proof(self):
        expression='value='+ '^'.join('t[%d]'%n for n in range(22,45))
        _,key,_,p=self.span(self.code(expression))
        self.assertIsNone(key['2']['words']);self.assertEqual(key['2']['reason'],'dense key')
        self.assertEqual(p['max_read_end'],720)

    def test_dynamic_and_unverified_alt_keep_full(self):
        _,_,_,p=self.span(self.code('value=t[t[22].z&127u]'))
        self.assertEqual(p['max_read_end'],65536)
        b=self.blobs(self.code('value=t[23]'));b['tssbo']=b'unknown future artifact'
        *_,p=policy.build_with_spans(b)
        self.assertEqual(p['2']['max_read_end'],65536)
        self.assertIn('proof',p['2']['reason'])

    def test_pointer_escape_keeps_unknown_span(self):
        from dmsh_uniform_predicates import _parse
        code=self.code('value=t[23]');module=self.root/'escape.spv';module.write_bytes(code)
        text=subprocess.check_output(['spirv-dis','--raw-id',str(module)],text=True)
        defs,rows,_,_=_parse(text)
        rid,_,a,index=next(row for row in rows if row[1]=='OpLoad' and
            defs.get(row[2][1],(None,None))[1] in ('OpAccessChain','OpInBoundsAccessChain'))
        lines=text.splitlines();typ=defs[a[1]][2][0]
        lines[index]=f'%escape = OpCopyObject {typ} {a[1]}\n{rid} = OpLoad {a[0]} %escape'
        asm=self.root/'escape.spvasm';asm.write_text('\n'.join(lines))
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(module)],check=True,capture_output=True)
        _,_,_,p=self.span(module.read_bytes());self.assertEqual(p['max_read_end'],65536)
        self.assertIn('pointer escape',p['reason'])

    def test_64bit_vector_width_crosses4096(self):
        # Vulkan1.1 relaxed block layout permits this 16B-aligned dvec4; its
        # 32B load starts4080 and ends4112. Do not check only the first u32.
        src=self.root/'wide.comp';out=self.root/'wide.spv'
        src.write_text('''#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=1) in;
layout(set=2,binding=1,std140) uniform Driver {layout(offset=4064)dvec4 v;};
layout(set=2,binding=2,std430) buffer Output {dvec4 value;};
void main(){value=v;}
''')
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(src),'-o',str(out)],check=True,capture_output=True)
        _,_,_,p=self.span(out.read_bytes());self.assertEqual(p['max_read_end'],4096)
        text=subprocess.check_output(['spirv-dis',str(out)],text=True)
        asm=self.root/'wide.spvasm';asm.write_text(text.replace('Offset 4064','Offset 4080'))
        subprocess.run(['spirv-as','--target-env','vulkan1.1',str(asm),'-o',str(out)],check=True,capture_output=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(out)],check=True,capture_output=True)
        _,_,_,p=self.span(out.read_bytes());self.assertEqual(p['max_read_end'],4112)

    def test_absent_and_removed_driver_binding_are_distinct(self):
        b=self.blobs(self.code('value=uvec4(1)'))
        *_,p=policy.build_with_spans(b)
        self.assertEqual(p['2']['max_read_end'],0)
        self.assertEqual(p['0']['binding'],0xffffffff)
        self.assertEqual(p['0']['max_read_end'],65536)

    def test_DDK1_exact_old_wire_and_optional_emit(self):
        b=self.blobs(self.code('value=t[23]'))
        body=struct.pack('<4I',policy.MAGIC,1,5,2)+policy.fingerprint(b)
        body+=struct.pack('<4I',0xffffffff,0xffffffff,1,2)+struct.pack('<2H',1,2)
        expected=body+hashlib.sha256(body).digest()
        key,proof=policy.build(b);self.assertEqual(key,expected)
        for suffix,data in b.items():(self.root/('test.'+suffix)).write_bytes(data)
        before=policy.emit(self.root,'test');self.assertEqual((self.root/'test.dkey').read_bytes(),expected)
        self.assertFalse((self.root/'test.dspan').exists())
        after=policy.emit_with_spans(self.root,'test')
        self.assertEqual(after['driver_keys'],before)
        self.assertEqual((self.root/'test.dkey').read_bytes(),expected)

    def test_all_verified_module_variants_scanned_once_per_binding(self):
        import dmsh_uniform_specialization as special
        import dmsh_uniform_predicates as predicates
        import dmsh_texel_ssbo as texel
        source=self.root/'all.frag';out=self.root/'all.spv'
        source.write_text('''#version 450
layout(set=1,binding=1,std140)uniform A{vec4 first[4096];};
layout(set=1,binding=2,std140)uniform B{vec4 second[4096];};
layout(set=1,binding=3)uniform samplerBuffer t;
layout(location=0)out vec4 color;
void main(){uint a=floatBitsToUint(first[0].x),b=floatBitsToUint(second[20].x);
uint s=0;for(uint i=0;i<min(a,4u);i++)s+=i+1u;
if(a==1u)s+=20u;else s+=40u;if((b&16u)!=0u)s+=100u;
color=texelFetch(t,0)+vec4(s,a,floatBitsToUint(second[20].x*2.0),b);}
''')
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(source),'-o',str(out)],check=True,capture_output=True)
        base=out.read_bytes();spec,sp=special.transform(base,{1:4},minimum_weight=0)
        self.assertIsNotNone(spec,sp)
        pred,pp=predicates.transform(base,spec,{1:4,2:5},minimum_weight=0);self.assertIsNotNone(pred,pp)
        alt,tp=texel.pair(base,spec,{3},1);self.assertIsNotNone(alt,tp)
        blobs={'spv':base,'spec':spec,'pred':pred,'tssbo':alt,
               'meta':struct.pack('<8I',0x314b5644,1,1,2,0,1,1,1)+
               struct.pack('<8i',1,1,0,0,0,1,0,0)+struct.pack('<8i',1,2,2,0,0,1,0,0)}
        modules=[base,spec[60:],predicates.unpack(pred,base,spec)[0],*texel.unpack_pair(alt,base,spec,tp)[:2]]
        with mock.patch.object(policy,'reads',wraps=policy.reads) as calls:
            *_,p=policy.build_with_spans(blobs,tp)
            self.assertEqual(len(calls.call_args_list),10)
            self.assertEqual([(c.args[0],c.args[2])for c in calls.call_args_list],[(m,b)for b in (1,2)for m in modules])
        for slot,binding in [('0',1),('2',2)]:
            expected=max(set().union(*(policy.reads(m,1,binding)for m in modules)),default=-4)+4
            self.assertEqual(p[slot]['max_read_end'],expected)

    def test_specialized_high_tail_controls_union(self):
        base=self.code('value=t[23]');special=self.code('value=t[300]')
        recipe=struct.pack('<7I',0x32505344,257,27,4,0,0,len(special)//4)+hashlib.sha256(base).digest()+special
        b=self.blobs(base);b['spec']=recipe
        *_,p=policy.build_with_spans(b)
        self.assertEqual(p['2']['max_read_end'],4816)

    def test_deploy_optional_regeneration_rejects_false_proof(self):
        path=Path(__file__).resolve().parents[4]/'scripts/dev/deploy-android-shaders.py'
        spec=importlib.util.spec_from_file_location('driver_span_deploy_test',path)
        deploy=importlib.util.module_from_spec(spec);spec.loader.exec_module(deploy)
        b=self.blobs(self.code('value=t[23]'))
        for suffix,data in b.items():(self.root/('test.'+suffix)).write_bytes(data)
        old={'driver_keys':policy.emit(self.root,'test')}
        self.assertFalse(deploy.verify_driver_artifacts(self.root,'test',old))
        reflection=policy.emit_with_spans(self.root,'test')
        self.assertTrue(deploy.verify_driver_artifacts(self.root,'test',reflection))
        with self.assertRaises(SystemExit):deploy.verify_driver_artifacts(self.root,'test',old)
        span=self.root/'test.dspan';good=span.read_bytes();bad=bytearray(good)
        struct.pack_into('<I',bad,60,4);bad[64:]=hashlib.sha256(bad[:64]).digest();span.write_bytes(bad)
        wrong=json.loads(json.dumps(reflection));wrong['driver_spans']['sha256']=hashlib.sha256(bad).hexdigest()
        wrong['driver_spans']['proof']['2']['max_read_end']=4
        with self.assertRaises(SystemExit):deploy.verify_driver_artifacts(self.root,'test',wrong)
        span.unlink()
        with self.assertRaises(SystemExit):deploy.verify_driver_artifacts(self.root,'test',reflection)


if __name__=='__main__':unittest.main()
