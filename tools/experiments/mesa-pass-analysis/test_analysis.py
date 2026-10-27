import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('mesa_csv_analysis',Path(__file__).with_name('analyze.py'));M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
def row(batch,time,event,params='',frame=0):return f'{frame},{batch},{time},{event},{params}\n'
def cb(batch,start,draws=20,passes=2):
    result=[row(batch,start,'start_cmd_buffer','command_buffer_handle=7, appName=game, oneTimeSubmit=1, simultaneousUse=0,')]
    for n in range(passes):result.extend([row(batch,start+10+n*20,'start_render_pass','width=1280, height=720, attachment_count=2, hasDepth=true, ubwc=CC, loadCPP=8, storeCPP=8,'),row(batch,start+20+n*20,'end_render_pass',f'drawCount={draws}, tiledRender=false, tilingDisableReason=Autotune selected sysmem,')])
    result.append(row(batch,start+80,'end_cmd_buffer',f'renderpasses={passes}, dispatches=0,'));return result

class CSVAnalysis(unittest.TestCase):
    def test_balanced_json_and_empty_parameters(self):
        p,q=M.params('a=, binInfo={"tiles":[{"x":1,"y":2},3]}, why=plain, comma continuation, z=4,')
        self.assertEqual(p['a'],'');self.assertEqual(p['binInfo'],'{"tiles":[{"x":1,"y":2},3]}');self.assertEqual(p['why'],'plain,comma continuation');self.assertFalse(any(q.values()))
    def test_duplicates_and_malformed_prefix_retained(self):
        r=M.analyze(['frame,batch,time_ns,event,\n','bad row\n',row(1,0,'start_cmd_buffer','a=1, a=2,'),row(1,10,'end_cmd_buffer')])
        self.assertEqual(len(r['quality']['invalid_rows']),1);self.assertEqual(r['quality']['parameter_issues'][0]['duplicate_keys'],['a'])
    def test_no_fixed_49_or_tail_300(self):
        r=M.analyze(cb(10,0)+cb(12,100)+[row(11,90,'start_cmd_buffer'),row(11,95,'end_cmd_buffer','renderpasses=0, dispatches=0,')])
        self.assertEqual(r['complete_CB_count'],3);self.assertEqual(r['CB_classes'][0]['complete_CB_count'],2);self.assertEqual(r['dominant_draw_workload_class'],r['CB_classes'][0]['class_id']);self.assertEqual(r['CB_classes'][0]['pass_groups'][0]['count'],4)
    def test_longest_startup_cb_not_main(self):
        source=cb(1,0)+cb(3,100)+[row(2,1000,'start_cmd_buffer'),row(2,1000000,'start_compute','cs_sha1=compile, local_size_x=8,'),row(2,5000000,'end_compute'),row(2,9999999,'end_cmd_buffer','renderpasses=0, dispatches=1,')]
        r=M.analyze(source);main=next(c for c in r['CB_classes'] if c['class_id']==r['dominant_draw_workload_class']);self.assertEqual(main['complete_CB_count'],2);self.assertEqual(main['aggregate_reported_draws'],80)
    def test_multiple_cbs_same_batch(self):
        r=M.analyze(cb(7,0)+cb(7,100));self.assertEqual(r['complete_CB_count'],2);self.assertEqual(r['batch_completeness'][0]['complete_CB_count'],2);self.assertEqual(len(r['CB_classes']),1)
    def test_missing_edges_preserved(self):
        r=M.analyze([row(0,5,'end_cmd_buffer'),row(1,10,'start_render_pass'),row(1,15,'end_render_pass'),row(2,20,'start_cmd_buffer')])
        self.assertEqual(r['complete_CB_count'],0);self.assertEqual(len(r['batch_completeness'][0]['missing_start']),1);self.assertEqual(len(r['batch_completeness'][2]['missing_end']),1);self.assertEqual(len(r['unowned_complete_interval_ids']),1)
    def test_negative_interval_not_counted(self):
        r=M.analyze([row(0,10,'start_cmd_buffer'),row(0,5,'end_cmd_buffer')]);self.assertEqual(r['complete_CB_count'],0);self.assertEqual(len(r['batch_completeness'][0]['negative_ranges']),1);self.assertEqual(len(r['batch_completeness'][0]['timestamp_backsteps']),1)
    def test_nested_and_overlapping_phases_not_added(self):
        source=[row(0,0,'start_cmd_buffer'),row(0,2,'start_render_pass','width=10, height=10, attachment_count=1,'),row(0,3,'start_binning_ib'),row(0,5,'start_draw_ib_gmem'),row(0,9,'end_binning_ib'),row(0,11,'end_draw_ib_gmem'),row(0,12,'end_render_pass','drawCount=8, tiledRender=true,'),row(0,20,'end_cmd_buffer','renderpasses=1, dispatches=0,')]
        r=M.analyze(source);c=r['CB_classes'][0];self.assertEqual(c['child_union_statistics']['sum_ns'],10);rp=c['pass_groups'][0];self.assertEqual(rp['sum_ns'],10);self.assertEqual(rp['nested_phase_statistics_inclusive_not_additive']['binning_ib']['sum_ns'],6);self.assertEqual(rp['nested_phase_statistics_inclusive_not_additive']['draw_ib_gmem']['sum_ns'],6)
    def test_metadata_mismatch_is_not_silently_repaired(self):
        source=cb(0,0);source[-1]=row(0,80,'end_cmd_buffer','renderpasses=3, dispatches=0,');r=M.analyze(source);self.assertFalse(r['command_buffers'][0]['count_matches']['renderpasses']);self.assertEqual(r['command_buffers'][0]['observed']['renderpasses'],2)
    def test_same_kind_overlap_is_not_certified(self):
        r=M.analyze([row(0,0,'start_cmd_buffer'),row(0,1,'start_render_pass'),row(0,2,'start_render_pass'),row(0,3,'end_render_pass','drawCount=1,'),row(0,4,'end_render_pass','drawCount=1,'),row(0,5,'end_cmd_buffer','renderpasses=2, dispatches=0,')])
        self.assertEqual(len(r['batch_completeness'][0]['same_kind_nested_starts']),1);self.assertFalse(r['command_buffers'][0]['enabled_child_pairs_unambiguous_and_complete'])
    def test_unpaired_child_keeps_envelope_but_marks_scope_unknown(self):
        r=M.analyze([row(0,0,'start_cmd_buffer'),row(0,1,'start_compute'),row(0,5,'end_cmd_buffer','renderpasses=0, dispatches=1,')])
        self.assertEqual(r['complete_CB_count'],1);self.assertFalse(r['command_buffers'][0]['enabled_child_pairs_unambiguous_and_complete']);self.assertIsNone(r['dominant_draw_workload_class'])
    def test_checkpoint_is_exact_prefix_not_host_window(self):
        first=b'frame,batch,time_ns,event,\n0,1,5,start_cmd_buffer,\n';later=first+b'0,1,10,end_cmd_buffer,\n'
        tail,meta=M.checkpoint_suffix(later,first);r=M.analyze(tail.decode().splitlines(True));self.assertEqual(r['complete_CB_count'],0);self.assertIsNone(meta['host_window_mapping']);self.assertEqual(len(r['batch_completeness'][0]['missing_start']),1)
    def test_unrelated_checkpoint_refused(self):
        with self.assertRaisesRegex(ValueError,'exact byte prefix'):M.checkpoint_suffix(b'new file',b'old file')
    def test_compute_groups_and_unknown_values(self):
        source=[row(0,0,'start_cmd_buffer'),row(0,10,'start_compute','cs_sha1=abc, local_size_x=8, num_groups_x=16,'),row(0,25,'end_compute'),row(0,30,'end_cmd_buffer','renderpasses=0, dispatches=1,')];r=M.analyze(source);g=r['CB_classes'][0]['compute_groups'][0];self.assertEqual(g['sum_ns'],15);self.assertIsNone(g['key']['local_size_y']);self.assertIsNone(r['time_domain']['host_BOOTTIME_mapping']);self.assertFalse(r['time_domain']['batch_is_frame'])

if __name__=='__main__':unittest.main(verbosity=2)
