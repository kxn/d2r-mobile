#!/usr/bin/env python3
"""Boundary tests for whole-run official profile comparison, without devices."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('compare_profile_runs', Path(__file__).with_name('compare-profile-runs.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class CompareRunsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, p, v):
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(v))

    def fixture(self, name, periods=(6000000, 4000000), extra=False):
        parent = self.root / name
        d = parent / (name + '-BASE')
        d.mkdir(parents=True)
        for f in ('trace.pftrace', 'counter.data'):
            (d / f).write_bytes((name + f).encode())
        capture = {'pid': 10, 'APK_sha256': 'apk-same', 'trace_sha256': m.sha(d / 'trace.pftrace'), 'package': 'test.app'}
        self.write(d / 'capture.json', capture)
        receipt = {'completed': True, 'TID': 11, 'Surface_MONOTONIC_bounds_ns': [200, 1200], 'Surface_BOOTTIME_bounds_ns': [300, 1300]}
        rp = parent / (name + '-BASE-counter-receipt.json')
        self.write(rp, receipt)
        self.write(parent / (name + '-receipt.json'), {'APK': {'apk_sha256': 'apk-same', 'host_library_sha256': 'elf-same'}, 'owner_TID': 11})
        self.write(parent / (name + '-launch.json'), {'requested_record_state_extra_cache': extra, 'actual_record_state_extra_cache': extra})
        self.write(d / 'before-state.json', {'time_ns': 100, 'area': 1, 'x': 1, 'y': 1, 'game_output': {'width': 100, 'height': 50}, 'buffer_sync': {'guest_submits': 100}, 'pool_uploads': {'used_bytes': 10}})
        self.write(d / 'after-state.json', {'time_ns': 1400, 'area': 1, 'x': 1, 'y': 1, 'game_output': {'width': 100, 'height': 50}, 'buffer_sync': {'guest_submits': 140}, 'pool_uploads': {'used_bytes': 410}})
        (d / 'analysis.csv').write_text('''"section","a","b","seconds"
"corrected_Surface_time_bounds",300,1300,0.000001

"section","intervals","mean_ms","p95_ms","max_ms"
"corrected_complete_Surface_intervals",32,20,25,35

"section","running_ms","running_ms_per_Surface_interval"
"whole_app_CPU",640,20

"section","tid","comm","running_ms","running_ms_per_Surface_interval"
"thread_CPU",11,"owner, contains comma",320,10
"thread_CPU",12,"worker",160,5

"section","tid","weighted_mhz"
"thread_CPU_frequency",11,1700
''')
        total = sum(periods)
        budget = {'PID': 10, 'TID': 11, 'Surface_intervals': 32, 'MONOTONIC_bounds_ns': [200, 1200], 'sampled_instruction_period_sum': total,
                  'exact_owner_counter_brackets': {'instructions:u': {'window_lower': 32000000, 'window_upper': 35200000, 'per_Surface_interval_lower': 1000000, 'per_Surface_interval_upper': 1100000}},
                  'disjoint_actual_phase_duties': [
                      {'key': ['replay', 'binding'], 'sampled_instruction_period': periods[0], 'sampled_Minstructions_per_Surface': periods[0] / 32e6},
                      {'key': ['outside', 'unknown'], 'sampled_instruction_period': periods[1], 'sampled_Minstructions_per_Surface': periods[1] / 32e6}],
                  'inputs_sha256': {'trace.pftrace': m.sha(d / 'trace.pftrace'), 'counter.data': m.sha(d / 'counter.data'), 'receipt': m.sha(rp), 'host_ELF': 'elf-same'}}
        self.write(parent / 'offline-analysis' / d.name / 'budget.json', budget)
        return d

    def arm(self, d, **kw):
        return m.Arm(d, d.name, **kw).result()

    def add_sample_records(self, d):
        p = d.parent / 'offline-analysis' / d.name / 'budget.json'
        budget = json.loads(p.read_text())
        rows = [{'time': 300 + i * 100, 'period': r['sampled_instruction_period'],
                 'phase': r['key'][0], 'duty': r['key'][1], 'chain': [f'leaf{i}', 'ancestor']}
                for i, r in enumerate(budget['disjoint_actual_phase_duties'])]
        budget['samples'] = len(rows)
        budget['top_self_by_duty'] = [dict(r, key=[*r['key'], f'leaf{i}']) for i, r in enumerate(budget['disjoint_actual_phase_duties'])]
        self.write(p, budget)
        sidecar = p.with_name('sample-chain-values.json')
        sidecar.write_text(json.dumps(rows))
        return p, sidecar

    def test_raw_counts_total_density_and_self_are_visible(self):
        d = self.fixture('SAMPLES'); _, path = self.add_sample_records(d)
        a = self.arm(d)
        self.assertEqual(a['sample_support']['status'], 'available')
        self.assertEqual(a['sample_support']['value']['samples'], 2)
        self.assertEqual(a['sample_support']['value']['samples_per_Surface'], 2 / 32)
        self.assertEqual(a['sampled_phase_duties']['value'][0]['sample_count'], 1)
        self.assertEqual(a['sampled_top_self']['value'][0]['sample_count'], 1)
        self.assertTrue(any(x['path'] == str(path) for x in a['sources']))
        r = m.compare(a, a)
        self.assertEqual(r['comparison']['top_self_changes_not_complete'][0]['reference_samples'], 1)
        self.assertIn('Ref samples|Cand samples', m.markdown(r))
        self.assertIn('samples / Surface', m.markdown(r))

    def test_raw_counts_follow_existing_group_members(self):
        d = self.fixture('GROUP-SAMPLES'); self.add_sample_records(d)
        a = self.arm(d, config={'groups': [{'label': 'merged', 'phase': '.*', 'duty': '.*'}]})
        self.assertEqual(a['sampled_phase_duties']['value'][0]['sample_count'], 2)
        self.assertEqual(a['sample_support']['value']['phase_duties'][0]['sample_count'], 1)

    def test_missing_raw_counts_do_not_invalidate_exact_or_become_zero(self):
        d = self.fixture('NO-SAMPLES')
        a = self.arm(d)
        self.assertEqual(a['sample_support']['status'], 'missing')
        self.assertEqual(a['exact_owner_counters']['status'], 'available')
        self.assertIsNone(a['sampled_phase_duties']['value'][0]['sample_count'])
        r = m.compare(a, a)
        self.assertIsNone(r['comparison']['all_disjoint_phase_duties_Minstructions_per_Surface'][0]['reference_samples'])

    def test_raw_window_count_sum_and_partition_must_match_budget(self):
        for name, mutate in [
                ('bounds', lambda r: r[0].update(time=1200)),
                ('count', lambda r: r.pop()),
                ('periodsum', lambda r: r[0].update(period=r[0]['period'] + 1)),
                ('partition', lambda r: r[0].update(phase='different')),
                ('booleanperiod', lambda r: r[0].update(period=True)),
                ('booleantime', lambda r: r[0].update(time=True)),
                ('chain', lambda r: r[0].update(chain=[]))]:
            with self.subTest(name=name):
                d = self.fixture('INVALID-' + name); _, p = self.add_sample_records(d)
                rows = json.loads(p.read_text()); mutate(rows); p.write_text(json.dumps(rows))
                a = self.arm(d)
                self.assertEqual(a['sample_support']['status'], 'invalid')
                self.assertIsNone(a['sampled_phase_duties']['value'][0]['sample_count'])
                self.assertEqual(a['exact_owner_counters']['status'], 'available')

    def test_raw_self_period_match_and_qualified_budget_required(self):
        d = self.fixture('SELF-PERIOD'); p, _ = self.add_sample_records(d)
        b = json.loads(p.read_text()); b['top_self_by_duty'][0]['key'][-1] = 'wrong-leaf'
        self.write(p, b)
        self.assertEqual(self.arm(d)['sample_support']['status'], 'invalid')
        d = self.fixture('RAW-IDENTITY'); self.add_sample_records(d)
        (d / 'counter.data').write_text('tamper')
        self.assertEqual(self.arm(d)['sample_support']['status'], 'invalid')

    def test_raw_samples_missing_top_entry_stays_not_ranked(self):
        a = self.fixture('TOP-A'); pa, _ = self.add_sample_records(a)
        b = self.fixture('TOP-B'); pb, _ = self.add_sample_records(b)
        budget = json.loads(pb.read_text()); budget['top_self_by_duty'] = budget['top_self_by_duty'][1:]
        self.write(pb, budget)
        r = m.compare(self.arm(a), self.arm(b))
        row = next(x for x in r['comparison']['top_self_changes_not_complete'] if x['key'][-1] == 'leaf0')
        self.assertEqual(row['reference_samples'], 1)
        self.assertIsNone(row['candidate_samples'])
        self.assertIsNone(row['delta'])

    def test_full_partition_and_new_cost_not_just_target(self):
        a = self.arm(self.fixture('OFF', (6000000, 4000000)))
        b = self.arm(self.fixture('ON', (3000000, 6000000), True))
        report = m.compare(a, b)
        self.assertEqual(report['qualification']['same_binary'], 'same')
        changes = report['comparison']['all_disjoint_phase_duties_Minstructions_per_Surface']
        self.assertEqual(len(changes), 2)
        self.assertLess(next(x for x in changes if x['key'] == ['replay', 'binding'])['delta'], 0)
        self.assertGreater(report['comparison']['top_new_sampled_costs'][0]['delta'], 0)
        self.assertEqual(len(report['net_conclusion']['unexplained_or_broad_buckets']), 1)
        self.assertEqual(a['CPU']['owner']['comm'], 'owner, contains comma')
        text = m.markdown(report)
        self.assertIn('same', text)
        self.assertIn('missing', text)

    def test_missing_is_not_zero(self):
        d = self.root / 'empty'; d.mkdir()
        a = self.arm(d)
        self.assertEqual(a['exact_owner_counters']['status'], 'missing')
        self.assertEqual(a['GPU']['status'], 'missing')
        r = m.compare(a, a)
        self.assertEqual(r['qualification']['same_binary'], 'unverified')
        self.assertIsNone(r['comparison']['frames']['FPS']['delta'])
        self.assertEqual(r['comparison']['all_disjoint_phase_duties_Minstructions_per_Surface'], [])
        rows = m.compare_rows([{'key': 'old', 'x': 7}], [], 'x')
        self.assertIsNone(rows[0]['candidate'])
        self.assertIsNone(rows[0]['delta'])

    def test_snapshot_denominator_not_surface_count(self):
        a = self.arm(self.fixture('COUNTERS'))
        c = a['state_counters']
        row = next(x for x in c['rows'] if x['key'] == 'pool_uploads.used_bytes')
        self.assertEqual(c['guest_submit_delta'], 40)
        self.assertEqual(a['frames']['value']['count'], 32)
        self.assertEqual(row['delta'], 400)
        self.assertEqual(row['per_guest_submit'], 10)
        self.assertTrue(c['brackets_Surface_window'])

    def test_counter_reset_not_negative_work_saved(self):
        d = self.fixture('RESET')
        a = json.loads((d / 'after-state.json').read_text()); a['pool_uploads']['used_bytes'] = 1
        self.write(d / 'after-state.json', a)
        r = next(x for x in self.arm(d)['state_counters']['rows'] if x['key'] == 'pool_uploads.used_bytes')
        self.assertEqual(r['status'], 'reset_or_invalid_window')
        self.assertIsNone(r['delta'])
        self.assertIsNone(r['per_guest_submit'])

    def test_budget_tamper_hash_or_frame_count_is_invalid(self):
        d = self.fixture('HASH'); (d / 'counter.data').write_text('changed')
        a = self.arm(d)
        self.assertEqual(a['exact_owner_counters']['status'], 'invalid')
        self.assertIsNone(a['sampled_phase_duties']['value'])
        self.assertTrue(any('hash mismatch' in s for s in a['issues']))
        d = self.fixture('FRAMES')
        (d / 'analysis.csv').write_text((d / 'analysis.csv').read_text().replace('intervals",32,', 'intervals",31,'))
        self.assertEqual(self.arm(d)['exact_owner_counters']['status'], 'invalid')

    def test_duplicate_or_nonpartition_phase_rejected(self):
        d = self.fixture('DUP')
        p = d.parent / 'offline-analysis' / d.name / 'budget.json'
        b = json.loads(p.read_text()); b['disjoint_actual_phase_duties'][1]['key'] = ['replay', 'binding']
        self.write(p, b)
        self.assertEqual(self.arm(d)['sampled_phase_duties']['status'], 'invalid')

    def test_unknown_not_lost_by_custom_group(self):
        d = self.fixture('GROUP')
        a = self.arm(d, config={'groups': [{'label': 'all', 'phase': '.*', 'duty': '.*'}]})
        r = m.compare(a, a)
        self.assertEqual(len(r['comparison']['all_disjoint_phase_duties_Minstructions_per_Surface']), 1)
        self.assertEqual(len(r['net_conclusion']['unexplained_or_broad_buckets']), 1)

    def test_exact_bracket_cross_difference_not_midpoint(self):
        a = self.arm(self.fixture('EXACT-A')); b = self.arm(self.fixture('EXACT-B'))
        b['exact_owner_counters']['value']['instructions:u'].update(per_Surface_interval_lower=800000, per_Surface_interval_upper=900000)
        r = m.compare(a, b)['comparison']['exact_owner_counters_per_Surface']['instructions:u']
        self.assertEqual(r['delta_interval'], [-300000, -100000])
        self.assertAlmostEqual(r['percent_interval'][0], (800000 / 1100000 - 1) * 100)

    def test_gpu_existing_integral_adapter_and_clock_scope(self):
        d = self.fixture('GPU'); gp = self.root / 'gpu.json'
        self.write(gp, {'scope': 'official global', 'arms': {'G1': {'trace_sha256': m.sha(d / 'trace.pftrace'),
                   'Surface_first_last_BOOTTIME_ns': [300, 1300], 'metrics': {'Clocks / Second': {'time_weighted_mean': 499e6, 'integral_per_complete_Surface': 10}}}}})
        a = self.arm(d, gpu=gp, gpu_arm='G1')
        self.assertEqual(a['GPU']['alignment'], 'same trace and complete Surface bounds')
        self.assertIn('not proof', a['GPU']['value']['clocks_note'])
        self.assertEqual(a['GPU']['value']['metrics'][0]['reported_mean'], 499e6)
        self.assertEqual(m.compare(a, a)['comparison']['global_GPU_integrals_per_Surface_check_alignment'][0]['reference'], 10)
        invalid = self.arm(d, gpu=gp)
        self.assertEqual(invalid['GPU']['status'], 'invalid')

    def test_malformed_csv_never_silently_shift_columns(self):
        d = self.fixture('CSV')
        (d / 'analysis.csv').write_text('section,a,b\nrow,1\n')
        with self.assertRaises(ValueError): self.arm(d)

    def test_identity_conflict_not_same_binary(self):
        d = self.fixture('IDENTITY')
        c = json.loads((d / 'capture.json').read_text()); c['APK_sha256'] = 'another'
        self.write(d / 'capture.json', c)
        a = self.arm(d)
        self.assertEqual(m.compare(a, a)['qualification']['same_binary'], 'invalid')

    def test_cli_writes_json_and_compact_markdown_with_provenance(self):
        a, b = self.fixture('CLI-OFF'), self.fixture('CLI-ON', extra=True)
        out = self.root / 'report'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(m.main([str(a), str(b), '--output', str(out), '--no-scheduler']), 0)
        report = json.loads((out / 'comparison.json').read_text())
        self.assertEqual(report['generator']['sha256'], m.sha(Path(m.__file__)))
        md = (out / 'comparison.md').read_text()
        self.assertLess(md.index('FPS'), md.index('互斥阶段'))
        self.assertNotIn('```json', md)

    def test_nonzero_official_sample_loss_is_exposed(self):
        d = self.fixture('LOSS')
        p = d.parent / 'offline-analysis' / d.name / 'budget.json'
        b = json.loads(p.read_text()); b['record_meta'] = {'record_stat': 'kernelspace_lost_records=3,userspace_truncated_stack_samples=1'}
        self.write(p, b)
        a = self.arm(d)
        self.assertTrue(any('lost_records=3' in s for s in a['issues']))
        self.assertTrue(any('truncated_stack_samples=1' in s for s in a['issues']))

    def test_gpu_different_trace_never_called_same_window(self):
        d = self.fixture('UNALIGNED'); gp = self.root / 'unaligned.json'
        self.write(gp, {'trace_sha256': 'unrelated', 'Surface_first_last_BOOTTIME_ns': [300, 1300],
                        'metrics': [{'name': 'GPU Busy', 'mean': 60}]})
        a = self.arm(d, gpu=gp)
        self.assertTrue(a['GPU']['alignment'].startswith('unverified'))
        self.assertIn('never process-exclusive', a['GPU']['scope'])

    def scheduler_tables(self):
        return {'scheduler_identity': [{'pid': '10', 'tid': '11', 'utid': '4'}],
                'scheduler_integrity': [{'overlapping_intervals': '0'}],
                'scheduler_state': [{'state': 'Running', 'duration_ns': '20000000'},
                                    {'state': 'S', 'duration_ns': '8000000'},
                                    {'state': 'R', 'duration_ns': '2000000'}],
                'scheduler_waker': [{'waker_category': 'same_process_other_thread', 'attributed_sleep_ns': '6000000'},
                                    {'waker_category': 'unknown_waker', 'attributed_sleep_ns': '2000000'}]}

    def test_official_scheduler_query_pid_clock_bounds_and_following_waker(self):
        sql = m.scheduler_sql(10, 11, 300, 1300, 32)
        self.assertIn('t.tid=11 AND p.pid=10', sql)
        self.assertIn('lead(s.waker_utid)', sql)
        self.assertLess(sql.index('lead(s.waker_utid)'), sql.index('comparison_clipped_states AS'))
        self.assertIn("s.next_state NOT IN ('R','R+')", sql)
        self.assertIn('s.next_ts!=s.ts+s.dur', sql)
        self.assertIn('WHERE dur>0', sql)
        self.assertNotIn('THEN trace_end()', sql)
        for args in [(0, 11, 300, 1300, 32), (10, 11, 1300, 300, 32), (10, 11, 300, 1300, 0), ('10', 11, 300, 1300, 32)]:
            with self.assertRaises(ValueError): m.scheduler_sql(*args)

    def test_scheduler_disjoint_states_and_unknown_waker_partition(self):
        r = m.scheduler_summary(self.scheduler_tables(), 2, [100, 30000100], 10)
        self.assertEqual(r['status'], 'available')
        self.assertEqual(r['value']['running_ms_per_Surface'], 2)
        self.assertEqual(r['value']['runnable_ms_per_Surface'], .2)
        self.assertEqual(r['value']['sleep_S_ms_per_Surface'], .8)
        self.assertEqual(r['value']['untracked_ms_per_Surface'], 0)
        self.assertEqual(r['value']['waker_categories_ms_per_Surface']['unknown_waker'], .2)

    def test_scheduler_running_disagreement_and_overlap_are_invalid(self):
        self.assertEqual(m.scheduler_summary(self.scheduler_tables(), 3, [100, 30000100], 10)['status'], 'invalid')
        self.assertEqual(m.scheduler_summary(self.scheduler_tables(), 2, [100, 20000100], 10)['status'], 'invalid')
        t = self.scheduler_tables(); t['scheduler_identity'].append({'pid': '10', 'tid': '11', 'utid': '5'})
        self.assertEqual(m.scheduler_summary(t, 2, [100, 30000100], 10)['status'], 'invalid')

    def test_scheduler_empty_track_is_missing_and_waker_incomplete_is_invalid(self):
        self.assertEqual(m.scheduler_summary({}, 2, [100, 30000100], 10)['status'], 'missing')
        t = self.scheduler_tables(); t['scheduler_waker'] = []
        self.assertEqual(m.scheduler_summary(t, 2, [100, 30000100], 10)['status'], 'invalid')

    def test_scheduler_boottime_receipt_conflict_stops_before_tool(self):
        d = self.fixture('CLOCK')
        rp = d.parent / (d.name + '-counter-receipt.json')
        receipt = json.loads(rp.read_text()); receipt['Surface_BOOTTIME_bounds_ns'] = [200, 1200]
        self.write(rp, receipt)
        tool = self.root / 'must-not-execute'; tool.write_text('not an executable')
        arm = m.Arm(d, 'CLOCK')
        result = m.run_scheduler(arm, tool, self.root / 'scheduler-out')
        self.assertEqual(result['status'], 'invalid')
        self.assertIn('BOOTTIME', result['reason'])
        self.assertFalse((self.root / 'scheduler-out').exists())
        missing = m.run_scheduler(arm, None, self.root / 'missing-out')
        self.assertEqual(missing['status'], 'missing')

    def test_scheduler_cached_official_reduction_requires_hashes(self):
        d = self.fixture('CACHE')
        csv = d / 'analysis.csv'
        csv.write_text(csv.read_text().replace('320,10', '0.0005,0.000015625'))
        arm = m.Arm(d, 'CACHE')
        tool = self.root / 'verified-tool-wrapper'; tool.write_text('must not execute')
        out = self.root / 'cached-scheduler'; out.mkdir()
        query = m.scheduler_sql(10, 11, 300, 1300, 32)
        (out / 'owner-scheduler.sql').write_text(query)
        (out / 'owner-scheduler.csv').write_text('section,utid,pid,tid\nscheduler_identity,4,10,11\n\nsection,state,duration_ns\nscheduler_state,Running,500\nscheduler_state,S,300\nscheduler_state,R,200\n\nsection,waker_category,attributed_sleep_ns\nscheduler_waker,unknown_waker,300\n\nsection,overlapping_intervals\nscheduler_integrity,0\n')
        self.write(out / 'receipt.json', {'returncode': 0, 'tool_sha256': m.sha(tool), 'trace_sha256': m.sha(d / 'trace.pftrace'),
                   'sql_sha256': m.sha(out / 'owner-scheduler.sql'), 'csv_sha256': m.sha(out / 'owner-scheduler.csv')})
        r = m.run_scheduler(arm, tool, out)
        self.assertEqual(r['status'], 'available')
        self.assertTrue(r['provenance']['reused_official_reduction'])

    def replay_only_fixture(self, name, ambiguous=False):
        d = self.fixture(name)
        (d.parent / 'offline-analysis' / d.name / 'budget.json').unlink()
        rp = d.parent / (name + '-receipt.json')
        receipt = json.loads(rp.read_text()); receipt.pop('owner_TID', None); self.write(rp, receipt)
        text = (d / 'analysis.csv').read_text()
        text = text.replace('"section","tid","comm","running_ms","running_ms_per_Surface_interval"', '"section","tid","comm","has_replay","running_ms","running_ms_per_Surface_interval"')
        text = text.replace('"thread_CPU",11,"owner, contains comma",320,10', '"thread_CPU",11,"owner, contains comma",1,320,10')
        text = text.replace('"thread_CPU",12,"worker",160,5', f'"thread_CPU",12,"worker",{1 if ambiguous else 0},160,5')
        (d / 'analysis.csv').write_text(text)
        return d

    def explicit_metadata(self, d, pid=10, owner=11):
        cap = json.loads((d / 'capture.json').read_text()); cap['APK_sha256'] = 'a' * 64; self.write(d / 'capture.json', cap)
        rp = self.root / (d.name + '-external.json')
        self.write(rp, {'identity': {'process': pid, 'apk': 'a' * 64}, 'owner': owner})
        spec = {'path': str(rp), 'sha256': m.sha(rp), 'fields': {'pid': 'identity.process', 'apk_sha256': 'identity.apk', 'owner_tid': 'owner'}}
        mp = self.root / (d.name + '-metadata.json'); self.write(mp, {'run_receipt': spec})
        return mp, rp

    def test_unique_official_replay_owner_without_budget_is_shared_with_scheduler(self):
        d = self.replay_only_fixture('OWNER-UNIQUE')
        a = m.Arm(d, 'unique'); owner = a.resolve_owner()
        self.assertEqual(owner['value']['tid'], 11)
        r = a.result()
        self.assertEqual(r['CPU']['owner']['tid'], '11')
        self.assertEqual(r['exact_owner_counters']['status'], 'missing')
        self.assertEqual(r['CPU']['owner_evidence'], owner)

    def test_ambiguous_replay_owner_refuses_busiest_thread(self):
        d = self.replay_only_fixture('OWNER-AMBIGUOUS', True)
        a = m.Arm(d, 'ambiguous')
        self.assertEqual(a.resolve_owner()['status'], 'missing')
        self.assertIsNone(a.result()['CPU']['owner'])
        tool = self.root / 'must-not-run'; tool.write_text('not executable')
        self.assertEqual(m.run_scheduler(a, tool, self.root / 'none')['status'], 'missing')

    def test_explicit_receipt_adapter_identity_and_hash_refuse(self):
        d = self.replay_only_fixture('OWNER-ADAPTER')
        meta, rp = self.explicit_metadata(d)
        a = m.Arm(d, 'explicit', metadata=meta)
        self.assertTrue(a.metadata_identity_verified)
        self.assertEqual(a.result()['CPU']['owner']['tid'], '11')
        self.assertEqual(a.result()['exact_owner_counters']['status'], 'missing')
        rp.write_text(rp.read_text() + ' ')
        with self.assertRaisesRegex(ValueError, 'SHA256'): m.Arm(d, 'tampered', metadata=meta)
        d = self.replay_only_fixture('OWNER-PID-MISMATCH'); meta, _ = self.explicit_metadata(d, pid=999)
        with self.assertRaisesRegex(ValueError, 'PID/APK'): m.Arm(d, 'wrongpid', metadata=meta)

    def test_owner_log_pinned_line_and_pid_then_conflict(self):
        d = self.replay_only_fixture('OWNER-LOG')
        log = self.root / 'owner.log'; log.write_text('I/tool(10): NVN pin tid=11 result=0\n')
        proof = {'path': str(log), 'sha256': m.sha(log), 'line': 1, 'text': log.read_text().strip(),
                 'regex': r'I/tool\((?P<pid>\d+)\): NVN pin tid=(?P<tid>\d+) result=0'}
        mp = self.root / 'log-metadata.json'; self.write(mp, {'owner_evidence': proof})
        a = m.Arm(d, 'log', metadata=mp)
        self.assertEqual(a.resolve_owner()['value']['tid'], 11)
        proof['line'] = 2; self.write(mp, {'owner_evidence': proof})
        with self.assertRaisesRegex(ValueError, 'line number/text'): m.Arm(d, 'wrongline', metadata=mp).resolve_owner()
        proof['line'] = 1; proof['text'] = 'different'; self.write(mp, {'owner_evidence': proof})
        with self.assertRaisesRegex(ValueError, 'line number/text'): m.Arm(d, 'wrongtext', metadata=mp).resolve_owner()
        log.write_text('I/tool(10): NVN pin tid=12 result=0\n'); proof.update(sha256=m.sha(log), text=log.read_text().strip()); self.write(mp, {'owner_evidence': proof})
        self.assertEqual(m.Arm(d, 'conflict', metadata=mp).resolve_owner()['status'], 'invalid')

    def test_owner_log_without_pid_requires_verified_receipt(self):
        d = self.replay_only_fixture('OWNER-NOPID')
        log = self.root / 'no-pid.log'; log.write_text('translation tid=11\n')
        proof = {'path': str(log), 'sha256': m.sha(log), 'line': 1, 'text': log.read_text().strip(), 'regex': r'translation tid=(?P<tid>\d+)'}
        mp = self.root / 'no-pid-meta.json'; self.write(mp, {'owner_evidence': proof})
        with self.assertRaisesRegex(ValueError, 'verified run receipt'): m.Arm(d, 'no-binding', metadata=mp).resolve_owner()
        meta, _ = self.explicit_metadata(d); content = json.loads(meta.read_text()); content['owner_evidence'] = proof; self.write(meta, content)
        self.assertEqual(m.Arm(d, 'bound', metadata=meta).resolve_owner()['value']['tid'], 11)

    def test_launch_adapter_requires_matching_capture_pid_and_apk_binding(self):
        d = self.replay_only_fixture('OWNER-LAUNCH'); meta, _ = self.explicit_metadata(d)
        launch = self.root / 'launch.json'; self.write(launch, {'pid': 999})
        content = json.loads(meta.read_text()); content['launch'] = {'path': str(launch), 'sha256': m.sha(launch)}; self.write(meta, content)
        with self.assertRaisesRegex(ValueError, 'launch PID'): m.Arm(d, 'bad-launch', metadata=meta)
        self.write(launch, {'pid': 10}); content['launch']['sha256'] = m.sha(launch); self.write(meta, content)
        self.assertEqual(m.Arm(d, 'good-launch', metadata=meta).launch['pid'], 10)

    def test_log_pid_conflict_and_relative_pinned_path_refuse(self):
        d = self.replay_only_fixture('OWNER-LOG-WRONGPID')
        log = self.root / 'wrongpid.log'; log.write_text('pid=999 tid=11\n')
        proof = {'path': str(log), 'sha256': m.sha(log), 'line': 1, 'text': log.read_text().strip(), 'regex': r'pid=(?P<pid>\d+) tid=(?P<tid>\d+)'}
        meta = self.root / 'wrongpid-meta.json'; self.write(meta, {'owner_evidence': proof})
        with self.assertRaisesRegex(ValueError, 'log PID'): m.Arm(d, 'refuse', metadata=meta).resolve_owner()
        proof['path'] = 'relative.log'; self.write(meta, {'owner_evidence': proof})
        with self.assertRaisesRegex(ValueError, 'absolute'): m.Arm(d, 'refuse-path', metadata=meta).resolve_owner()

    def test_nested_runtime_policy_switches_are_visible(self):
        a, b = self.fixture('POLICY-A'), self.fixture('POLICY-B')
        for directory, enabled in ((a, False), (b, True)):
            p = directory.parent / (directory.parent.name + '-launch.json')
            launch = json.loads(p.read_text())
            launch['performance_hints'] = False
            launch['actual_game_state'] = {
                'present_worker': {'reference': not enabled, 'active': enabled},
                'performance_hint': {'reference': True},
            }
            self.write(p, launch)
        result = m.compare(self.arm(a), self.arm(b))
        switches = result['qualification']['changed_switches']
        self.assertEqual(switches['launch.actual_game_state.present_worker.active'],
                         {'reference': False, 'candidate': True})
        self.assertEqual(switches['launch.actual_game_state.present_worker.reference'],
                         {'reference': True, 'candidate': False})
        self.assertNotIn('launch.performance_hints', switches)
        self.assertIn('present_worker.active', m.markdown(result))

    def test_configurable_policy_paths_keep_missing_and_reject_objects(self):
        d = self.fixture('POLICY-EXPLICIT')
        a = self.arm(d, config={'switch_paths': ['before.area', 'before.not_present']})
        self.assertEqual(a['identity']['switches']['before.area'], 1)
        self.assertIsNone(a['identity']['switches']['before.not_present'])
        for bad in ('unknown.area', 'before.game_output', 'before..area', 'before'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.arm(d, config={'switch_paths': [bad]})

    def test_trace_owner_membership_rejection_removes_cpu_owner(self):
        d = self.replay_only_fixture('OWNER-TRACE-BINDING'); a = m.Arm(d, 'binding')
        self.assertEqual(a.resolve_owner()['value']['tid'], 11)
        reply = m.metric({'owner': {'pid': '999', 'tid': '11'}})
        rejected = m.bind_scheduler_owner(a, reply, 10, 11)
        self.assertEqual(rejected['status'], 'invalid')
        self.assertIsNone(a.result()['CPU']['owner'])
        a = m.Arm(d, 'valid-binding')
        m.bind_scheduler_owner(a, m.metric({'owner': {'pid': '10', 'tid': '11'}}), 10, 11)
        self.assertTrue(a.resolve_owner()['value']['trace_PID_TID_verified'])


if __name__ == '__main__':
    unittest.main()
