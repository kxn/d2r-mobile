#!/usr/bin/env python3
"""Deployment-only empty DPL fallback validation; no Android device required."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).with_name('deploy-android-shaders.py')
SPEC = importlib.util.spec_from_file_location('android_shader_deploy', SCRIPT)
deploy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deploy)
import dmsh_pixel_local as policy
import dmsh_pixel_local_wire as wire


class EmptyPixelLocalDeployment(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)

        def compile_shader(name, text, stage='frag'):
            source = cls.root/(name+'.'+stage); source.write_text(text)
            target = cls.root/(name+'.spv')
            subprocess.run(['glslangValidator', '-V', '--target-env', 'vulkan1.1',
                            str(source), '-o', str(target)], check=True, capture_output=True)
            return target.read_bytes()

        constant = compile_shader('constant', '#version 450\nlayout(location=0)out vec4 c;void main(){c=vec4(1);}')
        sampled = compile_shader('sampled', '#version 450\nlayout(set=1,binding=1)uniform sampler2D t;'
            'layout(location=1)in vec2 uv;layout(location=0)out vec4 c;void main(){c=texture(t,uv);}')
        rejected = compile_shader('rejected', '#version 450\nlayout(location=0)in vec3 p;'
            'void main(){gl_Position=vec4(p,1);if(p.x>0)gl_Position.x+=1;}', 'vert')
        compute = compile_shader('compute', '#version 450\nlayout(local_size_x=1)in;void main(){}', 'comp')
        texel = compile_shader('texel', '#version 450\nlayout(set=1,binding=2)uniform samplerBuffer t;'
            'layout(location=0)out vec4 c;void main(){c=texelFetch(t,int(gl_FragCoord.x));}')

        def fixture(name, blobs, stage=1, descriptor_set=1, extra=None):
            blobs = dict(blobs)
            blobs['meta'] = struct.pack('<3I', 0x324b5644, stage, descriptor_set)
            reflection = {'stage': ('vs','ps','gs','hs','ds','cs')[stage], 'descriptor_set': descriptor_set}
            reflection.update(extra or {})
            for suffix, code in blobs.items(): (cls.root/(name+'.'+suffix)).write_bytes(code)
            reflection['pixel_local'] = policy.emit(cls.root, name, reflection)
            payload, reflection['pixel_local_runtime'] = wire.build(blobs, reflection)
            return blobs, reflection, payload, (cls.root/(name+'.plocal.json')).read_bytes(), stage, descriptor_set

        cls.empty = fixture('empty', {'spv': constant})
        cls.nonempty = fixture('nonempty', {'spv': sampled})
        cls.rejected = fixture('rejected', {'spv': rejected}, 0, 0)
        cls.compute = fixture('compute', {'spv': compute}, 5, 2)
        recipe = struct.pack('<7I', 0x31505344, 1, 1, 4, 0, 0, len(texel)//4)+hashlib.sha256(texel).digest()+texel
        from dmsh_texel_ssbo import pair
        pair_blob, pair_proof = pair(texel, recipe, {2}, 1)
        cls.paired = fixture('paired', {'spv': texel, 'spec': recipe, 'tssbo': pair_blob},
                             extra={'texel_storage_alternative': pair_proof})

    def valid(self, fixture=None):
        return deploy.verify_empty_pixel_local(*(fixture or self.empty))

    def reseal(self, fixture, payload=None, report=None):
        blobs, reflection, original, report_data, stage, descriptor_set = copy.deepcopy(fixture)
        payload = original if payload is None else payload
        if report is not None:
            report_data = (json.dumps(report, sort_keys=True)+'\n').encode()
            reflection['pixel_local']['sha256'] = hashlib.sha256(report_data).hexdigest()
        reflection['pixel_local_runtime']['sha256'] = hashlib.sha256(payload).hexdigest()
        return blobs, reflection, payload, report_data, stage, descriptor_set

    def test_empty_never_normalizes_or_builds_variants(self):
        with patch.object(policy, 'analyze', side_effect=AssertionError('eligibility rerun')), \
             patch.object(wire, 'analyze', side_effect=AssertionError('eligibility rerun')), \
             patch.object(wire, 'build', side_effect=AssertionError('variant rebuild')):
            self.assertTrue(self.valid())
            self.assertTrue(self.valid(self.rejected))
            self.assertTrue(self.valid(self.compute))
            self.assertTrue(self.valid(self.paired))

    def test_all_header_fields_and_exact_empty_extent_are_checked(self):
        for offset, value in [(0,0), (4,2), (8,196), (12,0), (12,2), (16,0),
                              (20,1), (24,188), (28,1)]:
            payload = bytearray(self.empty[2]); struct.pack_into('<I', payload, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(SystemExit):
                self.valid(self.reseal(self.empty, bytes(payload)))
        for payload in (self.empty[2][:-1], self.empty[2]+bytes(4)):
            with self.subTest(size=len(payload)), self.assertRaises(SystemExit):
                self.valid(self.reseal(self.empty, payload))

    def test_five_member_closure_including_absent_members_is_checked(self):
        for slot in range(5):
            payload = bytearray(self.empty[2]); payload[32+slot*32] ^= 1
            with self.subTest(slot=slot), self.assertRaises(SystemExit):
                self.valid(self.reseal(self.empty, bytes(payload)))
        blobs, reflection, payload, report, stage, ds = copy.deepcopy(self.paired)
        blobs['spec'] = blobs['spec'][:-1]+bytes([blobs['spec'][-1]^1])
        with self.assertRaises(SystemExit): self.valid((blobs, reflection, payload, report, stage, ds))

    def test_source_module_set_and_each_actual_module_hash_are_checked(self):
        self.assertEqual(set(self.paired[1]['pixel_local_runtime']['source_modules']),
                         {'base','spec','tssbo','tssbo_spec'})
        for name in self.paired[1]['pixel_local_runtime']['source_modules']:
            fixture = copy.deepcopy(self.paired)
            fixture[1]['pixel_local_runtime']['source_modules'][name] = '0'*64
            with self.subTest(name=name), self.assertRaises(SystemExit): self.valid(fixture)
        for name, value in [('missing', None), ('extra', '0'*64)]:
            fixture = copy.deepcopy(self.empty); modules = fixture[1]['pixel_local_runtime']['source_modules']
            modules.pop('base') if value is None else modules.update({name:value})
            with self.subTest(name=name), self.assertRaises(SystemExit): self.valid(fixture)

    def test_actual_module_unpacking_remains_mandatory(self):
        with patch.object(wire, 'actual_modules', side_effect=ValueError('invalid paired closure')) as unpack:
            with self.assertRaisesRegex(ValueError, 'invalid paired closure'): self.valid(self.paired)
        unpack.assert_called_once()

    def test_reflection_hash_closure_schema_and_source_are_checked(self):
        original = json.loads(self.empty[3])
        for change in ('schema', 'verified', 'artifacts', 'missing', 'source', 'module_schema', 'stage'):
            report = copy.deepcopy(original)
            if change == 'schema': report['schema'] = 2
            if change == 'verified': report['closure_verified'] = False
            if change == 'artifacts': report['artifacts']['spv'] = '0'*64
            if change == 'missing': report['modules'].pop('base')
            if change == 'source': report['modules']['base']['source_sha256'] = '0'*64
            if change == 'module_schema': report['modules']['base']['schema'] = 2
            if change == 'stage': report['modules']['base']['stage'] = 'Vertex'
            with self.subTest(change=change), self.assertRaises(SystemExit):
                self.valid(self.reseal(self.empty, report=report))
        fixture = copy.deepcopy(self.empty); fixture[1]['pixel_local']['sha256'] = '0'*64
        with self.assertRaises(SystemExit): self.valid(fixture)

    def test_stage_metadata_and_rejected_sentinel_cannot_be_confused(self):
        for fixture in (self.empty, self.rejected, self.compute):
            changed = copy.deepcopy(fixture); changed[1]['stage'] = 'gs'
            with self.subTest(stage=fixture[4]), self.assertRaises(SystemExit): self.valid(changed)
        report = json.loads(self.rejected[3]); report['modules']['base'].pop('rejected')
        with self.assertRaises(SystemExit): self.valid(self.reseal(self.rejected, report=report))
        payload = bytearray(self.empty[2]); struct.pack_into('<I', payload, 12, 5)
        with self.assertRaises(SystemExit): self.valid(self.reseal(self.empty, bytes(payload)))

    def test_nonempty_record_cannot_be_disguised_as_empty(self):
        changed = copy.deepcopy(self.nonempty); changed[1]['pixel_local_runtime']['records'] = 0
        with self.assertRaises(SystemExit): self.valid(changed)
        payload = bytearray(self.nonempty[2]); struct.pack_into('<I', payload, 20, 0)
        with self.assertRaises(SystemExit): self.valid(self.reseal(changed, bytes(payload)))
        self.assertFalse(self.valid(self.nonempty))

    def run_verifier(self, fixture):
        blobs, reflection, payload, report, stage, ds = fixture
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)/'cache'; cache.mkdir(); output = Path(temporary)/'output'; output.mkdir()
            for suffix, data in {**blobs, 'plocal':payload, 'plocal.json':report}.items():
                (cache/('key.'+suffix)).write_bytes(data)
            return deploy.verify_pixel_local(cache, output, 'key', reflection, stage, ds,
                {'pixel_local_runtime':True, 'pixel_local_reflection':True})

    def test_integrated_empty_path_skips_both_reflection_and_wire_rebuilds(self):
        with patch.object(policy, 'emit', side_effect=AssertionError('reflection rebuild')), \
             patch.object(wire, 'build', side_effect=AssertionError('wire rebuild')):
            self.assertEqual(self.run_verifier(self.empty), 'empty_fallback')

    def test_nonempty_retains_both_real_regenerations_and_exact_comparison(self):
        with patch.object(policy, 'emit', wraps=policy.emit) as emit, \
             patch.object(wire, 'build', wraps=wire.build) as build:
            self.assertEqual(self.run_verifier(self.nonempty), 'regenerated')
        emit.assert_called_once(); build.assert_called_once()
        changed = copy.deepcopy(self.nonempty); payload = bytearray(changed[2]); payload[-1] ^= 1
        changed = self.reseal(changed, bytes(payload))
        with self.assertRaises(SystemExit): self.run_verifier(changed)


if __name__ == '__main__':
    unittest.main()
