#!/usr/bin/env python3
"""Owned offline receipt negatives; synthetic receipt PASS is never GPU evidence."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location('bulk_gate_test', HERE / 'compose-bulk-turnip-native-overlay.py')
bulk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bulk)
gate = bulk.producer_gate


class Gate(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.producer = '80' * 32
        self.source_sha = 'aa' * 32
        self.uuid = dict(layout_UUID='01' * 16, device_UUID='02' * 16, driver_UUID='03' * 16)
        self.legacy = dict(driver_sha256='ab' * 32, identity=dict(actual_recipe_UUID_payload_input_equal_table={
            'layout_uuid': self.uuid['layout_UUID'], 'device_uuid': self.uuid['device_UUID'], 'driver_uuid': self.uuid['driver_UUID']}))
        self.ready = dict(canonical=dict(APK=dict(sha256='11' * 32), native_libraries={'libdmhost_android.so': '22' * 32}),
                          ordinary_UID_controller=dict(sha256='33' * 32))
        manifest = self.root / 'batch.tsv'
        manifest.write_text('owned bounded input manifest')
        self.prepared = dict(entries=[], batches=[dict(ordinal=0, manifest=dict(path=str(manifest), sha256=bulk.digest_file(manifest)), entry_ids=[])])
        self.outputs = {}
        self.receipts = []
        for ordinal in range(2):
            identity = f'{ordinal + 1:064x}'
            self.prepared['entries'].append(dict(id=identity, input=dict(sha256=f'{ordinal + 7:064x}'), source=dict(sha256=f'{ordinal + 10:064x}')))
            self.prepared['batches'][0]['entry_ids'].append(identity)
            target = self.root / 'items' / identity
            target.mkdir(parents=True)
            payload = b'owned synthetic physical payload' + bytes([ordinal])
            (target / 'native-resource.bin').write_bytes(payload)
            metadata = dict(complete=True, driverID=18, producer_source_sha256=self.producer,
                input_sha256=f'{ordinal + 7:064x}', declaration_sha256=f'{ordinal + 10:064x}',
                original_driver_roundtrip_compressed_blocks_exact=True, physical_image_bytes=len(payload), **self.uuid)
            (target / 'native-resource.json').write_text(json.dumps(metadata))
            wire = bytearray(992)
            wire[:8] = b'DMNRES1\0'
            wire[112:160] = bytes.fromhex(''.join(self.uuid.values()))
            wire[288:320] = bytes.fromhex(self.producer)
            (target / 'native-resource-layout.bin').write_bytes(wire)
            receipt = dict(ordinal=ordinal, entry_id=identity, complete=True, input_sha256=metadata['input_sha256'],
                declaration_sha256=metadata['declaration_sha256'], native_payload_sha256=bulk.digest_file(target / 'native-resource.bin'),
                native_metadata_sha256=bulk.digest_file(target / 'native-resource.json'), layout_record_sha256=bulk.digest_file(target / 'native-resource-layout.bin'),
                producer_source_sha256=self.producer, physical_image_bytes=len(payload), original_driver_roundtrip_compressed_blocks_exact=True, **self.uuid)
            self.receipts.append(receipt)
        self.complete = dict(schema=1, complete=True, entries=2, physical_image_bytes=sum(r['physical_image_bytes'] for r in self.receipts),
            producer_source_sha256=self.producer, manifest_sha256=bulk.digest_file(manifest), GPU_sampling_executed=False, game_modules_executed=False)
        self.result = dict(stage='actual-turnip-native-resource-batch-owned-passed', cleanup_errors=[], original_unchanged=True,
            fixture_PID_absent=True, fixture_uninstalled=True, fixture_tmp_absent=True, owned_clients_stopped=True,
            original_before={'owned original': True}, original_after={'owned original': True}, prepare_manifest_sha256=self.source_sha,
            APK_sha256=self.ready['canonical']['APK']['sha256'], native_libraries=self.ready['canonical']['native_libraries'],
            driver_sha256=self.legacy['driver_sha256'], controller_sha256=self.ready['ordinary_UID_controller']['sha256'], batch_index=0,
            batch_manifest_sha256=bulk.digest_file(manifest), entry_count=2, runs=[dict(result='passed', first_pid=123, PID_absent_after=True,
                queried_identity=self.uuid, rows=[dict(native_image_app_configuration=True, uid=10101, game_modules_executed=False)])])
        self.query = self.root / 'items' / self.prepared['entries'][0]['id'] / 'native-resource.json'
        self.index = dict(schema=1, complete=True, producer_source_sha256=self.producer, source_pack_manifest_sha256=self.source_sha)

    def tearDown(self):
        self.temp.cleanup()

    def write(self):
        (self.root / 'resource-batch-receipts.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in self.receipts))
        (self.root / 'resource-batch-complete.json').write_text(json.dumps(self.complete))
        self.result['actual_batch_complete'] = self.complete
        leaves = ['resource-batch-receipts.jsonl', 'resource-batch-complete.json']
        leaves += ['items/' + e['id'] + '/' + leaf for e in self.prepared['entries'][:2]
                   for leaf in ['native-resource.bin', 'native-resource.json', 'native-resource-layout.bin']]
        self.result['baked_outputs'] = {p: bulk.digest_file(self.root / p) for p in leaves}
        result_path = self.root / 'result.json'
        result_path.write_text(json.dumps(self.result))
        self.index['results'] = [dict(path=str(result_path), sha256=bulk.digest_file(result_path))]
        path = self.root / 'actual-index.json'
        path.write_text(json.dumps(self.index))
        return path

    def check(self, path=None):
        path = self.write() if path is None else path
        return gate.actual_batches_gate(path, bulk.digest_file(path), self.ready, self.producer, self.prepared,
            self.source_sha, self.legacy, self.query, bulk.Fingerprints())

    def test_owned_synthetic_complete_receipt_coverage(self):
        self.assertEqual(set(self.check()), {e['id'] for e in self.prepared['entries']})

    def test_incomplete_actual_index_rejected(self):
        self.index['complete'] = False
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.check()

    def test_pending_or_absent_actual_batch_rejected(self):
        path = self.write()
        value = json.loads(path.read_text())
        value['results'] = []
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'no actual'):
            self.check(path)

    def test_subset_cannot_certify_full_selection(self):
        item = dict(id='ff' * 32, input=dict(sha256='ee' * 32), source=dict(sha256='dd' * 32))
        self.prepared['entries'].append(item)
        self.prepared['batches'].append(dict(ordinal=1, manifest={}, entry_ids=[item['id']]))
        with self.assertRaisesRegex(ValueError, 'complete selection'):
            self.check()

    def test_duplicate_actual_batch_rejected(self):
        path = self.write()
        value = json.loads(path.read_text())
        value['results'] *= 2
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'duplicate actual batch'):
            self.check(path)

    def test_failed_cleanup_rejected(self):
        self.result['cleanup_errors'] = ['owned process not stopped']
        with self.assertRaisesRegex(ValueError, 'cleanup'):
            self.check()

    def test_wrong_canonical_APK_rejected(self):
        self.result['APK_sha256'] = 'ff' * 32
        with self.assertRaisesRegex(ValueError, 'canonical'):
            self.check()

    def test_root_UID_cannot_be_ordinary_UID_evidence(self):
        self.result['runs'][0]['rows'][0]['uid'] = 0
        with self.assertRaisesRegex(ValueError, 'ordinary-UID'):
            self.check()

    def test_older_producer_receipt_cannot_be_relabeled(self):
        self.receipts[0]['producer_source_sha256'] = '34' * 32
        with self.assertRaisesRegex(ValueError, 'source/identity'):
            self.check()

    def test_changed_source_declaration_rejected(self):
        self.receipts[0]['declaration_sha256'] = 'ff' * 32
        with self.assertRaisesRegex(ValueError, 'source/identity'):
            self.check()

    def test_changed_payload_rejected_even_with_new_outer_file_index(self):
        self.query.with_name('native-resource.bin').write_bytes(b'corrupt changed bytes')
        with self.assertRaisesRegex(ValueError, 'three-file'):
            self.check()

    def test_legacy_query_path_rejected(self):
        original = self.query
        self.query = self.root / 'legacy-single-query.json'
        self.query.write_bytes(original.read_bytes())
        with self.assertRaisesRegex(ValueError, 'current actual batch output'):
            self.check()


if __name__ == '__main__':
    unittest.main()
