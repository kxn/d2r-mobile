#!/usr/bin/env python3
"""BC3 preparation checks use the real NVN C layout and real producer validator."""
import argparse, ctypes as C, hashlib, importlib.util, json, os, struct, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('bc3_prepare',ROOT/'scripts/dev/prepare-native-image-resource.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
PRODUCER=Path(os.environ.get('DM_BC3_HOST_PRODUCER',str(ROOT/'.local/build/native-bc3-producer-v1/host/native_image_payload')))
PROOF=Path(os.environ.get('DM_BC3_PRODUCER_READY',str(ROOT/'.local/build/native-bc3-producer-v1/source-ready.json')))
SHA=json.loads(PROOF.read_text())['producer_source_sha256'] if PROOF.exists() else 'a'*64


def source(game,width,height,levels):
    descriptor={'filename':'data/hd/test.texture','format':game,'type':2,'depth':1,'layoutType':3,'width':width,'height':height,'levels':levels,'layoutData':0}
    nvn=m.BC3_FORMATS[game][0];desc=m.repack.desc(descriptor,nvn);offsets=m.repack.geometry(desc)
    storage=C.create_string_buffer(offsets[-1]);blocks=[]
    for level in range(levels):
        w,h=m.repack.dimensions(width,height,level);raw=bytes((i+level*31)%256 for i in range(m.repack.block_bytes(w,h,4)))
        m.repack.swizzle(desc,storage,level,raw,4);blocks.append(raw)
    prefix=bytearray(36+8*levels);prefix[:4]=b'<DE(';prefix[4]=game;prefix[6]=2
    struct.pack_into('<II',prefix,8,width,height);struct.pack_into('<H',prefix,16,1);prefix[18]=3
    struct.pack_into('<Q',prefix,19,0);struct.pack_into('<I',prefix,28,levels);struct.pack_into('<I',prefix,32,4)
    data=m.repack.rebuild_table(bytes(prefix),36,[storage.raw[offsets[n]:offsets[n+1]] for n in range(levels)])
    return descriptor,data,blocks


class PrepareBC3(unittest.TestCase):
    def prepare(self,folder,game=61,w=13,h=9,levels=4):
        descriptor,data,blocks=source(game,w,h,levels)
        class Context:
            entries={'data/hd/test.texture':descriptor}
            common={'source_profile':'original-bc3-raw-casc-v1','intended_producer_sha256':SHA}
            def read(self,logical):return data,{'sha256':m.sha(data),'logical':logical}
        args=argparse.Namespace(original_bc3=True,output=folder,logical='data/hd/test.texture')
        result=m.prepare(args,Context());return result,blocks

    def test_both_BC3_small_NPOT_and_full_mips(self):
        for game in (61,62):
            for width,height,levels in [(1,1,1),(4,4,3),(13,9,4),(17,5,5)]:
                with self.subTest(game=game,width=width,height=height), tempfile.TemporaryDirectory() as temp:
                    out=Path(temp)/'input';result,blocks=self.prepare(out,game,width,height,levels)
                    declaration=json.loads((out/'resource-source.json').read_text());wire=(out/'resource-input.bin').read_bytes()
                    self.assertEqual(result['VkFormat'],137 if game==61 else 138)
                    self.assertEqual(declaration['intended_producer_sha256'],SHA)
                    self.assertFalse(declaration['decoder_or_encoder_used']);self.assertNotIn('linear_ASTC_bytes',declaration)
                    for n,raw in enumerate(blocks):
                        _,_,offset,size,digest=struct.unpack_from('<IIQQ32s',wire,152+56*n)
                        self.assertEqual(wire[offset:offset+size],raw);self.assertEqual(digest,hashlib.sha256(raw).digest())
                    if PRODUCER.exists():
                        run=subprocess.run([str(PRODUCER),'--verify-resource-input',str(out)],capture_output=True,text=True)
                        self.assertEqual(run.returncode,0,run.stderr);self.assertIn('"GPU_executed":false',run.stdout)

    @unittest.skipUnless(PRODUCER.exists(),'new host producer not built')
    def test_real_C_consumer_rejects_wrong_format_tuple(self):
        for game in (61,62):
            with self.subTest(game=game), tempfile.TemporaryDirectory() as temp:
                out=Path(temp)/'input';self.prepare(out,game)
                p=out/'resource-input.bin';wire=bytearray(p.read_bytes())
                struct.pack_into('<I',wire,36,138 if game==61 else 137)
                wire[1048:1080]=bytes(32);wire[1048:1080]=hashlib.sha256(wire).digest();p.write_bytes(wire)
                run=subprocess.run([str(PRODUCER),'--verify-resource-input',str(out)],capture_output=True,text=True)
                self.assertNotEqual(run.returncode,0);self.assertIn('h->game_format',run.stderr)

    def test_original_context_rejects_mixed_pack_and_missing_identity(self):
        args=argparse.Namespace(pack_index=Path('processed'),data_dir=None,pack_manifest=[],composition_report=None,producer_sha256=SHA)
        with self.assertRaisesRegex(ValueError,'mixed'):m.OriginalBC3Context(args)
        args.pack_index=None;args.producer_sha256=None
        with self.assertRaisesRegex(ValueError,'producer SHA256'):m.OriginalBC3Context(args)

    def test_original_and_ASTC_entrypoints_do_not_accept_each_others_formats(self):
        descriptor,data,_=source(61,4,4,3)
        class Context:
            common={};entries={'data/hd/test.texture':descriptor}
            def read(self,logical):return data,{'sha256':m.sha(data)}
        with tempfile.TemporaryDirectory() as temp:
            args=argparse.Namespace(original_bc3=False,logical='data/hd/test.texture',output=Path(temp)/'input')
            with self.assertRaisesRegex(ValueError,'explicitly selected'):m.prepare(args,Context())
        descriptor['format']=95
        with tempfile.TemporaryDirectory() as temp:
            args=argparse.Namespace(original_bc3=True,logical='data/hd/test.texture',output=Path(temp)/'input')
            with self.assertRaisesRegex(ValueError,'explicitly selected'):m.prepare(args,Context())


if __name__=='__main__':unittest.main()
