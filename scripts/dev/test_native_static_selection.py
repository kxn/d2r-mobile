#!/usr/bin/env python3
"""Offline source-selection/receipt negatives, never GPU execution evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
import native_static_selection as policy
import native_bulk_producer_gate as gate


def write(root, name, value):
    path = root / name; path.write_text(json.dumps(value))
    return policy.record(path)


class Selection(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = Path(self.tmp.name)
        self.name = 'data/hd/items/misc/gold/gold_alb.texture'
        self.desc = dict(format=81, type=2, depth=1, layoutType=3, width=128, height=128,
                         levels=8, layoutData=0, thumbnailBase64='', thumbnailMip=0)
        self.cache = dict(sha256='ab' * 32); self.index = dict(sha256='bc' * 32)
        self.array_record = dict(path='/only-offline/closure', sha256='cd' * 32)
        self.source = dict(schema=1, source_cache=self.cache, source_index=self.index,
                           array_closure=self.array_record, entries=[dict(logical=self.name, descriptor=self.desc)])
        self.old = dict(schema=1, complete=True, selection=dict(kind=policy.LEGACY_KIND, array_closure=self.array_record),
                        source_context=dict(source_cache=self.cache, source_index=self.index), entries=[dict(logical='data/hd/character/old.texture')])
        self.value = dict(schema=1, kind=policy.KIND, selected_logicals=[self.name],
                          source_receipt=write(self.root, 'catalog.json', self.source),
                          reuse_source_pack=write(self.root, 'old.json', self.old))
        self.ctx = SimpleNamespace(entries={self.name:self.desc}, cache_info=self.cache, common=dict(source_index=self.index))

    def tearDown(self): self.tmp.cleanup()

    def run_selection(self, arrays=()):
        item = write(self.root, 'logical.json', self.value)
        return policy.explicit(Path(item['path']), item['sha256'], self.ctx, set(arrays))

    def rejects(self, arrays=()):
        with self.assertRaises((ValueError, KeyError)): self.run_selection(arrays)

    def test_positive_explicit_not_immutability(self):
        names, result = self.run_selection(); self.assertEqual(names, [self.name])
        self.assertTrue(result['static_file_origin_is_not_runtime_immutability'])

    def test_duplicate(self): self.value['selected_logicals'] *= 2; self.rejects()
    def test_bad_hash(self):
        item = write(self.root, 'logical.json', self.value)
        with self.assertRaises(ValueError): policy.explicit(Path(item['path']), '00' * 32, self.ctx, set())
    def test_noncanonical_and_UI(self):
        for name in ['data/hd/items/../x.texture','Data/hd/items/x.texture','data/hd/ui/x.texture','data/hd/a\\b.texture']:
            with self.assertRaises(ValueError): policy.logical_name(name)
    def test_array(self): self.rejects([self.name])
    def test_existing_must_reuse(self):
        self.old['entries'].append(dict(logical=self.name)); self.value['reuse_source_pack']=write(self.root,'old.json',self.old); self.rejects()
    def test_catalog_not_authoritative(self):
        self.ctx.entries[self.name] = dict(self.desc, width=64); self.rejects()
    def test_wrong_source_identity(self):
        self.ctx.cache_info={'sha256':'ff'*32}; self.rejects()
    def test_tampered_provenance(self):
        Path(self.value['source_receipt']['path']).write_text('{}'); self.rejects()
    def test_bad_shape(self):
        for changes in [dict(type=3),dict(depth=2),dict(width=4097),dict(width=True),dict(levels=9),dict(layoutData=6),dict(format=99),dict(thumbnailBase64='eA==',thumbnailMip=0)]:
            self.assertFalse(policy.eligible(dict(self.desc, **changes)))
    def test_incomplete_reuse(self):
        self.old['complete']=False; self.value['reuse_source_pack']=write(self.root,'old.json',self.old); self.rejects()
    def test_unknown_logical(self):
        self.ctx.entries={}; self.rejects()
    def test_array_census_mismatch(self):
        self.source['array_closure']={};self.value['source_receipt']=write(self.root,'catalog.json',self.source);self.rejects()
    def test_quality_missing_bad_and_hash(self):
        p=self.root/'quality.tsv';p.write_text('file\tx\tw\th\ty\tPSNR\n'+self.name+'\tx\t128\t128\ty\t30\n')
        record=dict(path=str(p),sha256=policy.sha(p),matches=True); prov=dict(source_and_artifact_verification=[record])
        self.assertEqual(policy.quality_gate(prov,[self.name],self.root)['sha256'],record['sha256'])
        with self.assertRaises(ValueError): policy.quality_gate(prov,['missing'],self.root)
        p.write_text(p.read_text().replace('\t30\n','\tNaN\n'))
        with self.assertRaises(ValueError): policy.quality_gate(prov,[self.name],self.root)
        record['sha256']=policy.sha(p)
        with self.assertRaises(ValueError): policy.quality_gate(prov,[self.name],self.root)
    def test_preparer_enforces_real_16MiB_bound_before_allocation(self):
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('bounded_resource', Path(__file__).with_name('prepare-native-image-resource.py'))
        resource = importlib.util.module_from_spec(spec); spec.loader.exec_module(resource)
        ctx = SimpleNamespace(entries={self.name:self.desc}, read=lambda name:(b'bounded test',{}))
        desc = SimpleNamespace(levels=1,width=1,height=1)
        args = SimpleNamespace(logical=self.name,output=self.root/'never-created')
        with patch.object(resource.repack,'full_texture',return_value=(0,0,desc,[0,resource.MAX_BYTES+1])):
            with self.assertRaisesRegex(ValueError,'exceeds 16MiB'): resource.prepare(args,ctx)
        self.assertFalse(args.output.exists())

    def test_existing_CASC_default_and_explicit_encoding(self):
        import sys, zlib
        sys.path[:0] = [str(Path(__file__).resolve().parents[2] / 'dm/tools'), str(Path(__file__).resolve().parents[2] / 'dm/ios/tools')]
        import casc_overlay as casc
        data = b'original source data'
        key = bytearray(16); key[10] = 8
        path = self.root / casc.data_filename(0,8)
        blte = casc.blte_encode(data); path.write_bytes(blte)
        self.assertEqual(casc.read_data_file(self.root,key,len(blte)), data)
        self.assertEqual(casc.read_data_file(self.root,key,len(blte),encoding='b:256K*=z'),data)
        packed=zlib.compress(data); path.write_bytes(packed)
        self.assertEqual(casc.read_data_file(self.root,key,len(packed),encoding='z'),data)
        path.write_bytes(data)
        self.assertEqual(casc.read_data_file(self.root,key,len(data),encoding='n'),data)
        with self.assertRaises(SystemExit): casc.read_data_file(self.root,key,len(data),encoding='unrecognized')

    def test_absent_actual_never_native(self):
        p=self.root/'index.json';p.write_text(json.dumps(dict(schema=1,complete=True,producer_source_sha256='ab'*32,source_pack_manifest_sha256='old')))
        class H:
            def file(self,path): return policy.sha(path)
        with self.assertRaisesRegex(ValueError,'mismatched actual'):
            gate.actual_batches_gate(p,policy.sha(p),{},'ab'*32,{},'new',{},None,H())


if __name__ == '__main__': unittest.main()
