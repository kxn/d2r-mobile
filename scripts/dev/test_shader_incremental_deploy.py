#!/usr/bin/env python3
"""Host orchestration checks using real cp/tar/SHA/mv; never invoke ADB."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('shader_incremental_deploy',
    Path(__file__).with_name('deploy-android-shaders.py'))
deploy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deploy)
REAL_RUN = subprocess.run


def identity(data):
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}


class LocalDevice:
    """Translate only transport/UID boundary; real standard tools execute scripts."""
    def __init__(self, root):
        self.root = root
        self.app = root/'app'; self.app.mkdir()
        self.tmp = root/'tmp'; self.tmp.mkdir()
        self.calls = []
        self.pushes = []
        self.active = False
        self.activate_after_hash = False
        self.corrupt_delta = False
        self.fail_publish = False

    def mapped(self, text):
        return text.replace('/data/local/tmp', str(self.tmp))

    def run(self, argv, **kwargs):
        self.calls.append(argv)
        assert argv[:3] == ['adb', '-s', 'host-test-only'], argv
        parts = argv[3:]
        if parts[:2] == ['shell', 'pidof']:
            return subprocess.CompletedProcess(argv, 0 if self.active else 1,
                '1234\n' if self.active else '', '')
        if parts[0] == 'push':
            source, dest = Path(parts[1]), Path(self.mapped(parts[2]))
            assert source.is_file(), 'must never push complete cache directory'
            self.pushes.append((source.name, dest.name))
            shutil.copyfile(source, dest)
            if self.corrupt_delta and dest.name == 'delta.tar':
                # A valid archive carrying incorrect bytes must fail final full SHA.
                import tarfile
                payload = self.root/'bad.spv'; payload.write_bytes(b'corrupt')
                with tarfile.open(dest, 'w') as archive:
                    archive.add(payload, arcname='changed.spv')
                    archive.add(self.root.parent/'cache/added.meta', arcname='added.meta')
            return subprocess.CompletedProcess(argv, 0, '', '')
        assert parts[0] == 'shell', parts
        if len(parts) == 2:
            words = shlex.split(parts[1])
            assert words[:3] == ['run-as', 'com.d2rmobile.nativeport', 'sh'], words
            assert words[3] == '-c' and len(words) == 5, words
            script = self.mapped(words[4])
            if self.fail_publish and 'if ! mv ' in script:
                # Force the existing rollback path, leaving all other mv real.
                script = script.replace('if ! mv ', 'if ! false ', 1)
            result = REAL_RUN(['sh', '-c', script], cwd=self.app, **kwargs)
            if self.activate_after_hash and 'sha256sum -c _deployment_sha256.txt' in script:
                self.active = True
            return result
        return REAL_RUN([self.mapped(x) for x in parts[1:]], cwd=self.app, **kwargs)


class IncrementalDeploy(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cache = self.root/'cache'; self.cache.mkdir()
        (self.root/'device').mkdir()
        self.device = LocalDevice(self.root/'device')
        self.target = self.device.app/'files/runtime/shaders'
        self.target.mkdir(parents=True)
        self.original = {'same.spv': b'unchanged', 'changed.spv': b'old', 'removed.meta': b'remove'}
        self.candidate = {'same.spv': b'unchanged', 'changed.spv': b'new', 'added.meta': b'add'}
        for name, data in self.original.items(): (self.target/name).write_bytes(data)
        for name, data in self.candidate.items(): (self.cache/name).write_bytes(data)
        self.inputs = {name: identity(data) for name, data in self.original.items()}
        self.artifacts = {name: identity(data) for name, data in self.candidate.items()}
        receipt = self.cache/'_derived_input_receipt.json'
        receipt.write_text(json.dumps({'artifacts': self.inputs})+'\n')
        self.receipt_sha = deploy.sha(receipt)
        self.args = SimpleNamespace(serial='host-test-only', report=self.root/'report.json',
            reuse_device_input=True, verified_cache_derived=True,
            derived_input_receipt_sha256=self.receipt_sha)

    def run_deploy(self):
        report = {}
        with patch.object(deploy.subprocess, 'run', side_effect=self.device.run):
            deploy.deploy_verified_artifacts(self.args, self.cache, self.artifacts, report)
        return report

    def assert_original_intact(self):
        self.assertTrue(self.target.is_dir())
        for name, data in self.original.items(): self.assertEqual((self.target/name).read_bytes(), data)
        self.assertFalse(any(self.target.parent.glob('.shaders-previous-*')))

    def test_exact_delta_plan_and_deleted_artifact(self):
        inputs, changed, removed = deploy.incremental_input_plan(self.cache, self.artifacts, self.receipt_sha)
        self.assertEqual(inputs, self.inputs)
        self.assertEqual(changed, ['added.meta', 'changed.spv'])
        self.assertEqual(removed, ['removed.meta'])

    def test_receipt_hash_and_symlink_must_match(self):
        with self.assertRaises(SystemExit):
            deploy.incremental_input_plan(self.cache, self.artifacts, '0'*64)
        receipt = self.cache/'_derived_input_receipt.json'
        receipt.rename(self.root/'receipt.json'); receipt.symlink_to(self.root/'receipt.json')
        with self.assertRaises(SystemExit):
            deploy.incremental_input_plan(self.cache, self.artifacts, self.receipt_sha)

    def test_invalid_artifact_names_and_identities_are_rejected(self):
        for name in ('..', '.', '../x', '/x', 'x/y', 'x\ny', '_deployment_check.log', '_deployment_sha256.txt', '-x'):
            with self.subTest(name=name), self.assertRaises(SystemExit):
                deploy.incremental_artifact_map({name: identity(b'x')}, 'test')
        for info in ({'sha256':'0'*63,'bytes':1}, {'sha256':'A'*64,'bytes':1},
                     {'sha256':'0'*64,'bytes':True}, {'sha256':'0'*64,'bytes':-1}, {}, None):
            with self.subTest(info=info), self.assertRaises(SystemExit):
                deploy.incremental_artifact_map({'x':info}, 'test')

    def test_inventory_allows_only_two_known_extra_names(self):
        deploy.shader_inventory_matches('./x\n./_deployment_sha256.txt\n./_deployment_check.log\n', {'x':{}}, 'test')
        for listing in ('./x\n./extra\n', './x\n./x\n', './other\n', 'x\n', './x\n./sub/file\n'):
            with self.subTest(listing=listing), self.assertRaises(SystemExit):
                deploy.shader_inventory_matches(listing, {'x':{}}, 'test')

    def test_only_derived_mode_and_before_any_device_operation(self):
        self.args.verified_cache_derived = False
        with self.assertRaises(SystemExit): self.run_deploy()
        self.assertEqual(self.device.calls, [])
        with patch.object(sys, 'argv', ['deploy', '--cache', str(self.cache), '--report', str(self.args.report),
                                       '--reuse-device-input']), \
             patch.object(deploy.subprocess, 'run', side_effect=AssertionError('unexpected device operation')):
            with self.assertRaisesRegex(SystemExit, 'requires --verified-cache-derived'): deploy.main()

    def test_real_copy_delta_hash_and_atomic_publish(self):
        for name in deploy.DEPLOYMENT_TRACES: (self.target/name).write_bytes(b'old publishing trace')
        report = self.run_deploy()
        self.assertTrue(report['deployed'])
        self.assertEqual(report['phone_verified_files'], len(self.artifacts))
        reuse = report['device_input_reuse']
        self.assertEqual((reuse['input_verified_files'], reuse['candidate_verified_files'], reuse['changed_files'],
                          reuse['changed_bytes'], reuse['removed_files']), (3,3,2,6,['removed.meta']))
        self.assertEqual(set(p.name for p in self.target.iterdir()), set(self.artifacts)|deploy.DEPLOYMENT_TRACES)
        for name, data in self.candidate.items(): self.assertEqual((self.target/name).read_bytes(), data)
        previous = self.device.app/report['previous']
        for name, data in self.original.items(): self.assertEqual((previous/name).read_bytes(), data)
        self.assertNotEqual((previous/'same.spv').stat().st_ino, (self.target/'same.spv').stat().st_ino)
        (self.target/'same.spv').write_bytes(b'after publication')
        self.assertEqual((previous/'same.spv').read_bytes(), b'unchanged')
        self.assertEqual([x[1] for x in self.device.pushes], ['input.sha256','cleanup.sh','delta.tar','candidate.sha256'])
        self.assertFalse(list(self.device.tmp.iterdir()))

    def test_unknown_file_directory_and_symlink_fail_before_copy(self):
        for kind in ('file', 'directory', 'link'):
            extra = self.target/'unknown'
            if kind == 'file': extra.write_bytes(b'x')
            elif kind == 'directory': extra.mkdir()
            else: extra.symlink_to(self.target/'same.spv')
            with self.subTest(kind=kind), self.assertRaises(SystemExit): self.run_deploy()
            self.assert_original_intact()
            self.assertFalse(list(self.target.parent.glob('.shaders-*')))
            if extra.is_dir(): extra.rmdir()
            else: extra.unlink()

    def test_declared_file_symlink_is_rejected(self):
        same = self.target/'same.spv'; same.unlink()
        outside = self.root/'outside'; outside.write_bytes(b'unchanged'); same.symlink_to(outside)
        with self.assertRaisesRegex(SystemExit, 'symlink'): self.run_deploy()
        self.assertTrue(same.is_symlink())
        self.assertFalse(list(self.target.parent.glob('.shaders-*')))

    def test_input_sha_failure_preserves_source_and_does_not_copy(self):
        (self.target/'same.spv').write_bytes(b'wrong input')
        with self.assertRaises(subprocess.CalledProcessError): self.run_deploy()
        self.assertEqual((self.target/'same.spv').read_bytes(), b'wrong input')
        self.assertFalse(list(self.target.parent.glob('.shaders-*')))
        self.assertEqual([x[1] for x in self.device.pushes], ['input.sha256'])

    def test_final_sha_failure_cannot_publish(self):
        self.device.corrupt_delta = True
        with self.assertRaises(subprocess.CalledProcessError): self.run_deploy()
        self.assert_original_intact()
        self.assertEqual(len(list(self.target.parent.glob('.shaders-*'))), 1)

    def test_running_game_refused_before_prepare_and_before_publish(self):
        self.device.active = True
        with self.assertRaisesRegex(SystemExit, 'stop the game'): self.run_deploy()
        self.assert_original_intact(); self.assertEqual(self.device.pushes, [])
        self.device.active = False; self.device.activate_after_hash = True
        with self.assertRaisesRegex(SystemExit, 'stop the game'): self.run_deploy()
        self.assert_original_intact()

    def test_atomic_publication_failure_restores_original(self):
        self.device.fail_publish = True
        with self.assertRaises(subprocess.CalledProcessError): self.run_deploy()
        self.assert_original_intact()


if __name__ == '__main__': unittest.main()
