#!/usr/bin/env python3
"""Host-only current886 qualification/policy checks; no execute-ready input or device.

The one full offline plan uses the real immutable artifact/resource evidence.
Negative cases mutate in-memory inputs/read results only, never historical cards.
"""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
R = Path('/home/dl/d2r-mobile')

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value)
    return value

g = load('current_sustained_runner', HERE / 'run-game.py')
s = load('current_sustained_helper', HERE / 'sustained_play.py')
c = load('current_sustained_candidate', HERE / 'candidate_gate.py')
a = load('current_sustained_assets', HERE / 'qualified_asset_gate.py')
PARENT = R / '.local/build/private-image-budget-74d7e17-Q735-pair-v1/root-execute/PARENT1.json'
DELIVERED = R / '.local/logs/lazy-physical-span-index-88620ec-desktop-Q735-v1/delivered-baseline.json'
ROUTE = R / '.local/build/native-sustained-play-v2/camp-route-v2.json'
LIVE_REVIEW = R / '.local/logs/current-sustained-886-fresh-baseline-v2/root-review.json'

def record(path):
    return {'path': str(path), 'sha256': g.sha(path)}

def current_input(live_review=None):
    p = g.read(PARENT)
    for key in ('official_cpu_aa', 'performance_tools', 'official_l1i_record',
                'official_memory_map', 'builtin_vma_on_suspend'):
        p.pop(key, None)
    p['baseline'] = record(DELIVERED)
    p['original_apk'] = g.read(g.pinned(p['canonical']))['canonical_APK']['archive']
    p['original_host_elf'] = g.read(DELIVERED)['host_ELF_SHA256']
    p['sustained_play'] = {'seconds': 1800, 'interval_seconds': 600, 'trace_seconds': 8,
        'current_acceptance': record(DELIVERED), 'route': record(ROUTE), 'resume_step': 0,
        'scheduler_helper': record(HERE / 'capture-sustained-scheduler.py')}
    if live_review is not None:
        p['sustained_play']['live_baseline_review'] = record(live_review)
        p['baseline'] = g.read(live_review)['fresh_baseline']
    p['execution_ready'] = False
    p['authorization_scope'] = 'Host-only offline current886 sustained plan; no device authorization supplied'
    p['output'] = str(R / '.local/logs/current-sustained-886-host-plan-only')
    return p

def prepared_input():
    p = current_input()
    can = g.read(g.pinned(p['canonical']))
    p['new_apk'] = can['canonical_APK_sha256']
    p['new_elf'] = can['canonical_native_library_sha256']['libdmhost_android.so']
    for phase in p['phases']:
        phase['composition_data'] = g.read(g.pinned(phase['composition']))
    return p

class CurrentQualification(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.p = prepared_input()

    def test_actual_current_qualification_without_relabeling_controlled_scope(self):
        p = copy.deepcopy(self.p)
        self.assertEqual(s.validate(p, g.pinned, g.read, g.require), s.CURRENT_RUNTIME)
        self.assertEqual(p['_sustained_route']['scope'], 'town')
        self.assertNotIn('_sustained_current', p)  # only proof() grants this token

    def test_old6f_actual_qualification_still_supported(self):
        p = g.read(R / '.local/build/native-sustained-play-v2/inputs-NATIVE-ONLY-v2.json')
        p['sustained_play']['scheduler_helper'] = record(HERE / 'capture-sustained-scheduler.py')
        for phase in p['phases']:
            phase['composition_data'] = g.read(g.pinned(phase['composition']))
        self.assertIsNone(s.validate(p, g.pinned, g.read, g.require))
        p['sustained_play']['live_baseline_review'] = record(LIVE_REVIEW)
        with self.assertRaisesRegex(RuntimeError, 'config fields'):
            s.validate(p, g.pinned, g.read, g.require)

    def test_old_runtime_or_wrong_baseline_restore_artifacts_refused(self):
        for key, value in [('runtime_commit', s.RUNTIME), ('original_host_elf', '0'*64),
                           ('baseline', self.p['canonical']), ('shared_lazy_windows', 0),
                           ('shared_image_raw_observe', 1), ('driver_span_reference', 0)]:
            p = copy.deepcopy(self.p); p[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                s.validate(p, g.pinned, g.read, g.require)

    def test_relabelled_publication_or_incomplete_actual_evidence_refused(self):
        for field in ('dynamic_HUD_settings_not_overridden', 'actual_saved_stopped_original_restored'):
            def changed(path):
                data = g.read(path)
                if field in data: data[field] = False
                return data
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                s.validate(copy.deepcopy(self.p), g.pinned, changed, g.require)

    def test_extra_capture_modes_and_wrong_route_are_refused(self):
        for key in ('official_cpu_aa', 'performance_tools', 'official_memory_map', 'builtin_vma_on_suspend'):
            p = copy.deepcopy(self.p); p[key] = {}
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                s.validate(p, g.pinned, g.read, g.require)
        p = copy.deepcopy(self.p); p['sustained_play']['resume_step'] = 2
        with self.assertRaises(RuntimeError): s.validate(p, g.pinned, g.read, g.require)

    def test_current_gate_token_required_at_candidate_and_asset_boundaries(self):
        with self.assertRaises(RuntimeError): c.validate(copy.deepcopy(self.p), g.pinned, g.read, g.require)
        for token in (None, s.RUNTIME):
            with self.subTest(token=token), self.assertRaises(RuntimeError):
                a.validate(copy.deepcopy(self.p), g.pinned, g.read, g.require,
                           compiler_runtime=s.CURRENT_RUNTIME, sustained_runtime=token)

    def test_current_adaptive_endpoints_preserve_output_window_and_ready_contracts(self):
        state = {'valid': True, 'world_input_ready': True, 'scale_3d': .75,
            'game_output': {'width': 2750, 'height': 1260},
            'surface_output': {'width': 2750, 'height': 1260},
            'guest_heap': {'shared': True}, 'pool_windows': {'window_bytes': 1048576},
            'shared_lazy_windows': {'active': True},
            'shared_image_raw_observe': {'enabled_policy': False},
            'driver_constants': {'span_reference': True},
            'private_image_cache': {'budget_bytes': 536870912}}
        p = {'_sustained_current': True, 'pool_window_bytes': 1048576}
        for scale in (.5, .6, .65, .75, 1.):
            state['scale_3d'] = scale; s.validate_state(state, p, g.require)
        for key, value in [('scale_3d', .49), ('scale_3d', True), ('world_input_ready', False),
                           ('surface_output', {'width': 1, 'height': 1}),
                           ('pool_windows', {'window_bytes': 16777216}),
                           ('shared_lazy_windows', {'active': False}),
                           ('shared_image_raw_observe', {'enabled_policy': True}),
                           ('driver_constants', {'span_reference': False}),
                           ('private_image_cache', {'budget_bytes': 268435456}),
                           ('pool_copy_accounting', {'enabled': True})]:
            bad = copy.deepcopy(state); bad[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                s.validate_state(bad, p, g.require)
        with self.assertRaises(RuntimeError): s.validate_state(state, {'pool_window_bytes':1048576}, g.require)

    def test_complete_original_offline_plan_keeps_single_phase_and_sustained_scope(self):
        # Original complete proof, now for both actual delivered and actual
        # root-reviewed fresh stopped snapshots. Neither input is executable.
        for live in (None, LIVE_REVIEW):
            with self.subTest(live=live), tempfile.TemporaryDirectory(prefix='current-sustained-plan-') as folder:
                p = current_input(live)
                path = Path(folder) / 'inputs.json'; path.write_text(json.dumps(p))
                text = io.StringIO()
                args = ['run-game.py', '--inputs', str(path), '--inputs-sha256', g.sha(path),
                        '--report', str(Path(p['output']) / 'result.json')]
                with patch.object(sys, 'argv', args), contextlib.redirect_stdout(text), \
                     patch.object(g.m.common, 'Device', side_effect=AssertionError('no device allowed')):
                    g.main()
                plan = json.loads(text.getvalue())
                self.assertTrue(plan['offline']); self.assertEqual(plan['runtime'], s.CURRENT_RUNTIME)
                for name in ('android-game-walk-to.py', 'game_state_json.py'):
                    self.assertEqual(plan['controller_sources'][str(R / 'scripts/dev' / name)],
                                     g.sha(HERE.parent / name))
                self.assertEqual(len(plan['phases']), 1)
                self.assertEqual(plan['scale_3d'], .75); self.assertFalse(plan['fixed_3d'])
                self.assertFalse(plan['pool_copy_accounting'])
                self.assertIn('1800s uninterrupted', plan['scope'])
                self.assertIn('not fixedAB, combat, whole-game or6GB', plan['scope'])
                self.assertNotIn('Shortcandidate', plan['scope'])
                self.assertEqual(plan['qualified_native_asset_validation']['replacements'], 2838)
                self.assertFalse(json.loads(path.read_text())['execution_ready'])
                print(json.dumps({'baseline_kind': 'reviewed-fresh' if live else 'delivered',
                                  'actual_full_offline_plan': plan}, sort_keys=True))


class CurrentLiveBaseline(unittest.TestCase):
    """Actual immutable cards; refusals alter read results in memory only."""
    def setUp(self):
        self.p = current_input(LIVE_REVIEW)
        self.delivered = g.read(DELIVERED)
        self.fresh_path = Path(self.p['baseline']['path'])
        self.review = g.read(LIVE_REVIEW)
        self.fresh = g.read(self.fresh_path)

    def invoke(self):
        def read(path):
            if Path(path) == self.fresh_path: return self.fresh
            if Path(path) == LIVE_REVIEW: return self.review
            return g.read(path)
        return s.current_baseline(self.p, self.delivered, g.pinned, read, g.require)

    def test_actual_stopped_fresh_three_existing_hash_changes_pass(self):
        self.invoke()
        self.assertEqual(len(self.review['changes']), 3)
        self.assertEqual(g.sha(DELIVERED), s.CURRENT_DELIVERED)

    def test_unreviewed_fresh_wrong_pin_or_incomplete_review_refused(self):
        p = copy.deepcopy(self.p); p['sustained_play'].pop('live_baseline_review')
        with self.assertRaises(RuntimeError): s.current_baseline(p, self.delivered, g.pinned, g.read, g.require)
        for key in ('same_delivery', 'game_PID_absent', 'immutable_identity_unchanged', 'only_temp_changed',
                    'original_Device_identity_passed', 'profile_and_publication_receipt_device_hashes_match',
                    'controller_lock_absent'):
            with self.subTest(key=key):
                old = self.review[key]; self.review[key] = False
                with self.assertRaises(RuntimeError): self.invoke()
                self.review[key] = old
        self.review['fresh_baseline'] = self.review['delivered_baseline']
        with self.assertRaises(RuntimeError): self.invoke()

    def test_each_core_identity_and_non_temp_inventory_change_refused(self):
        original = copy.deepcopy(self.fresh)
        for key in ('APK_sha256', 'host_ELF_SHA256', 'model', 'serial', 'chain', 'profile', 'publication_receipt'):
            with self.subTest(key=key):
                self.fresh[key] = 'changed'
                with self.assertRaises(RuntimeError): self.invoke()
                self.fresh[key] = original[key]
        for target in original['inventories']:
            if target == 'files/runtime/temp': continue
            with self.subTest(target=target):
                self.fresh['inventories'][target] = {'unexpected-file': '0'*64}
                with self.assertRaises(RuntimeError): self.invoke()
                self.fresh['inventories'][target] = original['inventories'][target]
        self.fresh['game_PID_absent'] = False
        with self.assertRaises(RuntimeError): self.invoke()

    def test_temp_file_add_remove_bad_hash_or_unreviewed_changes_refused(self):
        temp = 'files/runtime/temp'; original = copy.deepcopy(self.fresh['inventories'][temp])
        self.fresh['inventories'][temp]['files/runtime/temp/new-file'] = '1'*64
        with self.assertRaises(RuntimeError): self.invoke()
        self.fresh['inventories'][temp] = copy.deepcopy(original)
        self.fresh['inventories'][temp].pop(next(iter(original)))
        with self.assertRaises(RuntimeError): self.invoke()
        self.fresh['inventories'][temp] = copy.deepcopy(original)
        changed = self.review['changes'][0]['path']
        self.fresh['inventories'][temp][changed] = 'not-a-SHA256'
        with self.assertRaises(RuntimeError): self.invoke()
        self.fresh['inventories'][temp] = copy.deepcopy(original)
        self.review['changes'].pop()
        with self.assertRaises(RuntimeError): self.invoke()

    def test_equal_file_sets_do_not_waive_temp_path_policy(self):
        # Synthetic extra old+new member solely tests the allowlist, not a
        # purported delivered snapshot or an actual independently approved card.
        temp = 'files/runtime/temp'; bad = temp + '/unrelated.data'
        self.delivered['inventories'][temp][bad] = '1'*64
        self.fresh['inventories'][temp][bad] = '2'*64
        with self.assertRaisesRegex(RuntimeError, 'unapproved temp hash'):
            self.invoke()

if __name__ == '__main__': unittest.main(verbosity=2)
