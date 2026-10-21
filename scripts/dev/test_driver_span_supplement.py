#!/usr/bin/env python3
"""Small offline provenance/wire fixtures; not shader eligibility or GPU tests."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('supplement', Path(__file__).with_name('compose-driver-span-supplement.py'))
supplement = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(supplement)


class SupplementContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.old = self.root/'vk-0123456789ab'; self.old.mkdir()
        self.new = self.root/'vk-fedcba987654'; self.new.mkdir()
        self.key = '0123456789abcdef'
        # No executable shader is compiled or analyzed: eligibility is a trusted
        # full-verifier receipt input in this contract-only fixture.
        bindings = struct.pack('<8i', 1, 7, 2, 0, 0, 1, 0, 0)
        meta = struct.pack('<12I', 0x324b5644, 1, 1, 1, 0, 0, 0, 0, 0xffffffff, 0, 0, 1)+bindings
        blobs = {'meta': meta, 'spv': b'fixture-only-immutable-module'}
        wire = struct.pack('<4I', supplement.driver.SPAN_MAGIC, 1, 1, 1)+supplement.driver.fingerprint(blobs)
        wire += struct.pack('<4I', 0xffffffff, 65536, 7, 1460)
        wire += hashlib.sha256(wire).digest()
        declaration = {'sha256': hashlib.sha256(wire).hexdigest(), 'proof': {
            '0': {'binding': 0xffffffff, 'max_read_end': 65536, 'reason': 'absent/ambiguous driver binding'},
            '2': {'binding': 7, 'max_read_end': 1460, 'reason': 'full verifier fixture receipt'}}}
        for cache, chain in [(self.old, '0123456789ab'), (self.new, 'fedcba987654')]:
            (cache/'.chain').write_text(chain+'\n')
            self.write(cache/'_vk_manifest.json', {'chain': chain, 'expected': 1, 'validated': 1, 'target': 'vulkan1.1'})
            self.write(cache/'_vk_build.json', [{'key': self.key, 'st': 'ok'}])
            for suffix, payload in blobs.items(): (cache/(self.key+'.'+suffix)).write_bytes(payload)
            reflection = {'chain': chain, 'key': self.key}
            if cache == self.new: reflection['driver_spans'] = declaration
            self.write(cache/(self.key+'.json'), reflection)
            (cache/(self.key+'.dkey')).write_bytes(b'unchanged DDK1 bytes')
            (cache/(self.key+'.plocal')).write_bytes(b'old DPL v1' if cache == self.old else b'large new DPL v2 is NOT transferred')
        (self.new/(self.key+'.dspan')).write_bytes(wire)
        self.inputs = self.root/'inputs.json'
        self.write(self.inputs, {'sources': [supplement.record(Path(module.__file__))
            for module in (supplement.driver, supplement.dmsh_uniform_predicates)]})
        self.tools = self.root/'tools.json'; self.tools.write_text('{}\n')
        self.archive = self.root/'source.tar'; self.archive.write_bytes(b'fixture producer source archive')
        self.old_receipt = self.root/'old.json'; self.new_receipt = self.root/'new.json'; self.ready = self.root/'ready.json'
        self.args = argparse.Namespace(baseline_cache=self.old, baseline_receipt=self.old_receipt,
            proof_cache=self.new, proof_receipt=self.new_receipt, proof_ready=self.ready,
            proof_build_inputs=self.inputs, output=self.root/'composed', verify_only=False)
        self.reseal()

    def write(self, path, value): path.write_text(json.dumps(value, sort_keys=True)+'\n')

    def reseal(self):
        for cache, path, deployed in [(self.old, self.old_receipt, True), (self.new, self.new_receipt, False)]:
            artifacts = supplement.inventory(cache)
            self.write(path, {'chain': (cache/'.chain').read_text().strip(), 'artifacts': artifacts,
                'original_proofs_unchanged': True, 'validated_game_and_builtin_shaders': 1,
                'deployed': deployed, 'phone_verified_files': len(artifacts), 'baseline_spirv_identical': True})
        self.write(self.ready, {'chain': 'fedcba987654', 'cache': str(self.new),
            'original_deploy_dryverify': supplement.record(self.new_receipt),
            'build_inputs': supplement.record(self.inputs), 'translator_tools': supplement.record(self.tools),
            'producer_source_archive': supplement.record(self.archive)})
        for name in ('baseline_receipt', 'proof_receipt', 'proof_ready', 'proof_build_inputs'):
            setattr(self.args, name+'_sha256', supplement.sha(getattr(self.args, name)))

    def run_compose(self): return supplement.compose(self.args, total=1, game_count=1)

    def test_preserves_old_bytes_and_does_not_reanalyze_or_transfer_new_DPL(self):
        old = supplement.inventory(self.old)
        with patch.object(supplement.driver, 'reads', side_effect=AssertionError('no new SPIR-V eligibility analysis')):
            report = self.run_compose()
            for name, expected in old.items(): self.assertEqual(supplement.sha(self.args.output/name), expected['sha256'])
            self.assertEqual(set(supplement.inventory(self.args.output))-set(old), {self.key+'.dspan', supplement.MANIFEST})
            self.assertEqual(report['DSP_bytes_added'], 96)
            self.assertEqual((self.args.output/'.chain').read_bytes(), (self.old/'.chain').read_bytes())
            self.args.verify_only = True
            self.assertTrue(self.run_compose()['verify_only'])
        self.assertEqual(supplement.inventory(self.old), old)

    def test_original_artifact_changed_is_rejected_without_partial_output(self):
        (self.old/(self.key+'.plocal')).write_bytes(b'changed old DPL')
        with self.assertRaisesRegex(ValueError, 'source inventory/hash'): self.run_compose()
        self.assertFalse(self.args.output.exists())

    def test_changed_module_is_rejected_even_with_resigned_base_receipt(self):
        (self.old/(self.key+'.spv')).write_bytes(b'different module')
        self.reseal()
        with self.assertRaisesRegex(ValueError, 'five-artifact bytes'): self.run_compose()

    def test_member_presence_must_match_not_only_base_SPV(self):
        (self.new/(self.key+'.pred')).write_bytes(b'new optional module')
        self.reseal()
        with self.assertRaisesRegex(ValueError, 'five-artifact presence'): self.run_compose()

    def test_bad_DSP_binding_even_when_digest_and_artifact_maps_are_resealed(self):
        path = self.new/(self.key+'.dspan'); payload = bytearray(path.read_bytes())
        struct.pack_into('<I', payload, 56, 8); payload[64:] = hashlib.sha256(payload[:64]).digest(); path.write_bytes(payload)
        path = self.new/(self.key+'.json'); reflection = json.loads(path.read_text())
        reflection['driver_spans']['proof']['2']['binding'] = 8
        reflection['driver_spans']['sha256'] = hashlib.sha256(payload).hexdigest(); self.write(path, reflection)
        self.reseal()
        with self.assertRaisesRegex(ValueError, 'DSP binding/end'): self.run_compose()

    def test_bad_DSP_fingerprint_even_when_top_level_hashes_are_resealed(self):
        path = self.new/(self.key+'.dspan'); payload = bytearray(path.read_bytes()); payload[16] ^= 1
        payload[64:] = hashlib.sha256(payload[:64]).digest(); path.write_bytes(payload)
        path = self.new/(self.key+'.json'); reflection = json.loads(path.read_text())
        reflection['driver_spans']['sha256'] = hashlib.sha256(payload).hexdigest(); self.write(path, reflection)
        self.reseal()
        with self.assertRaisesRegex(ValueError, 'wire/fingerprint/digest'): self.run_compose()

    def test_bad_supplement_manifest_is_rejected(self):
        self.run_compose(); self.args.verify_only = True
        path = self.args.output/supplement.MANIFEST; manifest = json.loads(path.read_text())
        manifest['proof_chain'] = manifest['base_chain']; self.write(path, manifest)
        with self.assertRaisesRegex(ValueError, 'supplement manifest changed'): self.run_compose()

    def test_extra_source_file_is_rejected(self):
        (self.new/'undeclared.bin').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError, 'source inventory/hash'): self.run_compose()

    def test_extra_output_file_is_rejected(self):
        self.run_compose(); self.args.verify_only = True
        (self.args.output/'undeclared.bin').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError, 'composed inventory/hash'): self.run_compose()

    def test_ready_must_pin_the_exact_completed_dryverify(self):
        ready = json.loads(self.ready.read_text()); ready['original_deploy_dryverify']['sha256'] = '0'*64
        self.write(self.ready, ready); self.args.proof_ready_sha256 = supplement.sha(self.ready)
        with self.assertRaisesRegex(ValueError, 'Ready receipt mismatch'): self.run_compose()

    def test_bad_receipt_pin_is_rejected(self):
        self.args.baseline_receipt_sha256 = '0'*64
        with self.assertRaisesRegex(ValueError, 'receipt SHA256 mismatch'): self.run_compose()

    def test_duplicate_manifest_members_are_rejected(self):
        self.run_compose(); self.args.verify_only = True
        (self.args.output/supplement.MANIFEST).write_text('{"schema":1,"schema":1}')
        with self.assertRaisesRegex(ValueError, 'duplicate JSON member'): self.run_compose()


if __name__ == '__main__': unittest.main()
