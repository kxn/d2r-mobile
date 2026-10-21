#!/usr/bin/env python3
"""Synthetic provenance/quality negatives only; not real texture/GPU acceptance."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
import native_astc6_selection as gate
import native_static_selection as policy

ROOT=Path('/home/dl/d2r-mobile')
EVALUATOR=ROOT/'.local/worktrees/astc-6x6-bulk-v1/dm/tools/astc_bulk.py'


class Selection(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.name='data/hd/character/fixture_alb.texture'
        self.entry=dict(filename=self.name,format=99,type=2,depth=1,layoutType=3,width=16,height=16,levels=1,layoutData=0,thumbnailBase64='',thumbnailMip=0)
        self.before=dict(self.entry,format=95)
        self.data=b'functional-file-hash-oracle-not-CFT'
        self.q=dict(mips=[dict(level=0,dimensions=[16,16],official_astcenc_metrics={'PSNR (LDR-RGB)':'42 dB'},source_alpha_range=[255,255],channels=dict(RGBA_max_abs=[0,0,0,0],alpha_coverage={'128':dict(before=256,after=256),'255':dict(before=256,after=256)}))])
        self.job=dict(job_key='test-job',quality_policy=copy.deepcopy(gate.QUALITY),source=dict(manifest_sha256='aa'*32),baseline=dict(full4_cache_sha256='bb'*32,full4_index_sha256='cc'*32),tool_hashes=[['astc_bulk.py',policy.sha(EVALUATOR)]])
        self.comp=dict(complete=True,source_clean=True,unselected_descriptors_and_index_exact=True,UI_and_full4_archive_exact=True,normal_entries_preserved=1,TVFS_proof=dict(VFS_only_selected_CFT_fields_changed=True),array_format_closure=dict(fixed_point=True,groups=[]),merged_index_sha256='dd'*32,final_cache_sha256='ee'*32,batch_job_key='test-job',selected=[self.name])
        self.old=dict(schema=1,complete=True,encoder_used=False,selection=dict(formats=[81,95]),source_context=dict(original_build_manifest=dict(sha256='aa'*32),source_cache=dict(sha256='bb'*32),source_index=dict(sha256='cc'*32)))
        self.item=dict(filename=self.name,status='converted',baseline_preserved=False,quality_failures=[],job_key='test-job')
        self.ctx=SimpleNamespace(entries={self.name:self.entry},common=dict(source_index=dict(sha256='dd'*32),original_build_manifest=dict(sha256='aa'*32)),cache_info=dict(sha256='ee'*32),read=lambda n:(self.data,{}))
        self.arrays=set()

    def tearDown(self):self.tmp.cleanup()
    def write(self,name,data):
        path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(json.dumps(data));return policy.record(path)
    def manifest(self):
        d=self.root/'items'/hashlib.sha256(self.name.encode()).hexdigest();d.mkdir(parents=True,exist_ok=True)
        (d/'texture').write_bytes(self.data)
        (d/'descriptor.json').write_text(json.dumps(self.entry));(d/'quality.json').write_text(json.dumps(self.q))
        item=dict(self.item,files={n:policy.sha(d/n)for n in ['texture','descriptor.json','quality.json']})
        report=dict(complete=True,job_key='test-job',results=[item]);job=self.write('job.json',self.job);quality=self.write('batch.json',report)
        before=self.write('before.json',dict(source_descriptor=self.before));old=dict(self.old,entries=[dict(logical=self.name,source=before)])
        old=self.write('old.json',old);comp=dict(self.comp,inputs={job['path']:job['sha256'],quality['path']:quality['sha256']});comp=self.write('composition.json',comp)
        value=dict(schema=1,kind=gate.KIND,selected_logicals=[self.name],qualified_composition=comp,encoder_job=job,encoder_report=quality,quality_evaluator=policy.record(EVALUATOR),original_native_source_pack=old)
        manifest=self.write('selection.json',value);return manifest,Path(comp['path'])
    def run_selection(self):
        item,comp=self.manifest();return gate.select(Path(item['path']),item['sha256'],self.ctx,self.arrays,comp)
    def rejected(self,reason):
        with self.assertRaisesRegex(ValueError,reason):self.run_selection()
    def test_positive_is_pending_actual_layout(self):
        names,proof=self.run_selection();self.assertEqual(names,[self.name]);self.assertTrue(proof['native_layout_query_pending'])
    def test_default_ASTC4_selection_unchanged(self):
        spec=importlib.util.spec_from_file_location('astc6_batch_policy',Path(__file__).with_name('prepare-native-image-batch.py'));mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        self.assertTrue(mod.selected(self.name,self.before,set()));self.assertFalse(mod.selected(self.name,self.entry,set()))
    def test_array(self):
        self.arrays={self.name};self.comp['array_format_closure']['groups']=[dict(ordered_members=[self.name])];self.rejected('array is excluded')
    def test_normal(self):
        self.name='data/hd/character/fixture_nrm.texture';self.entry['filename']=self.name;self.before['filename']=self.name;self.comp['selected']=[self.name];self.ctx.entries={self.name:self.entry};self.rejected('normal/declared')
    def test_UI(self):
        self.name='data/hd/ui/fixture.texture';self.rejected('UI is excluded')
    def test_unsupported_format(self):self.entry['format']=95;self.rejected('unsupported ASTC6')
    def test_non2D_and_bad_mip_shape(self):
        for change in [dict(depth=2),dict(type=3),dict(levels=6),dict(width=4097),dict(layoutData=6)]:
            saved=dict(self.entry);self.entry.update(change);self.rejected('unsupported ASTC6');self.entry.clear();self.entry.update(saved)
    def test_quality_fallback(self):self.item['status']='quality_fallback';self.rejected('fallback is not accepted')
    def test_quality_policy_cannot_weaken(self):self.job['quality_policy']['max_alpha_error']=9;self.rejected('policy differs')
    def test_original_alpha_and_rgb_metrics(self):
        for changes in ['alpha','constant','PSNR','coverage']:
            saved=copy.deepcopy(self.q);m=self.q['mips'][0]
            if changes=='alpha':m['source_alpha_range']=[0,255];m['channels']['RGBA_max_abs'][3]=9
            elif changes=='constant':m['channels']['RGBA_max_abs'][3]=1
            elif changes=='PSNR':m['official_astcenc_metrics']['PSNR (LDR-RGB)']='31 dB'
            else:m['channels']['alpha_coverage']['128']['after']=250
            self.rejected('per-mip RGB/alpha quality rejected');self.q=saved
    def test_payload_must_match_CASC(self):self.ctx.read=lambda n:(b'changed',{});self.rejected('CASC payload differs')
    def test_topology_must_match_original(self):self.before['width']=8;self.rejected('descriptor topology')
    def test_original_ancestry(self):self.job['baseline']['full4_index_sha256']='00'*32;self.rejected('source ancestry')
    def test_array_census_must_match(self):self.arrays={self.name};self.rejected('census differs')
    def test_preservation_proof(self):self.comp['UI_and_full4_archive_exact']=False;self.rejected('preservation proof')
    def test_manifest_hash(self):
        item,comp=self.manifest()
        with self.assertRaisesRegex(ValueError,'selection fingerprint'):gate.select(Path(item['path']),'00'*32,self.ctx,self.arrays,comp)

if __name__=='__main__':unittest.main(verbosity=2)
