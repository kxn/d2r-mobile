#!/usr/bin/env python3
"""Check startup-only allocation selection without contacting any device."""
import contextlib
import ast
import io
from pathlib import Path
import runpy
import re
import unittest
from unittest.mock import patch


LAUNCHER = Path(__file__).with_name('android-game-launch.py')


class LaunchCaptured(Exception):
    def __init__(self, command):
        self.command = command


class AttachedBackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Load the real validator without running the device-facing CLI.
        function = next(node for node in ast.parse(LAUNCHER.read_text()).body
                        if isinstance(node, ast.FunctionDef) and node.name == 'validate_shared_heap_backing')
        namespace = {'re': re}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(LAUNCHER), 'exec'), namespace)
        cls.validate = staticmethod(namespace['validate_shared_heap_backing'])

    def state(self, window):
        return {'guest_heap': {'shared': True, 'backing_initialized': True},
                'pool_windows': {'window_bytes': window}}

    def test_attach_accepts_proven_whole_pool_and_window_backings(self):
        for window in (0, 1048576):
            self.validate(self.state(window),
                          '[dm] [shared pool] immutable startup shared; import_window_bytes='+str(window),
                          attach=True, expected_window=0)

    def test_attach_rejects_missing_or_conflicting_startup_proof(self):
        for log in ('', '[shared pool] immutable startup shared; import_window_bytes=0'):
            with self.assertRaisesRegex(RuntimeError, 'disagree'):
                self.validate(self.state(1048576), log, attach=True, expected_window=0)

    def test_attach_rejects_uninitialized_or_invalid_backing(self):
        for window in (None, True, -1, 4097):
            with self.assertRaisesRegex(RuntimeError, 'invalid'):
                self.validate(self.state(window), '', attach=True, expected_window=0)
        state = self.state(1048576)
        state['guest_heap']['backing_initialized'] = False
        with self.assertRaisesRegex(RuntimeError, 'initialized'):
            self.validate(state, '', attach=True, expected_window=0)

    def test_cold_launch_still_requires_explicit_nonzero_window(self):
        with self.assertRaisesRegex(RuntimeError, 'whole-pool'):
            self.validate(self.state(1048576), '', attach=False, expected_window=0)
        self.validate(self.state(1048576), '', attach=False, expected_window=1048576)


class PoolWindowLaunchTests(unittest.TestCase):
    def test_record_state_cache_is_explicit_default_off_and_cold_only(self):
        self.assertNotIn('record_state_cache',self.launch_command([]))
        for option,value in [('--record-state-cache','true'),('--no-record-state-cache','false')]:
            command=self.launch_command([option]);index=command.index('record_state_cache')
            self.assertEqual(command[index-1:index+2],['--ez','record_state_cache',value])
            with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2)
            adb.assert_not_called()

    def test_record_state_extra_cache_is_explicit_incremental_and_cold(self):
        self.assertNotIn('record_state_extra_cache', self.launch_command([]))
        for option,value in [('--record-state-extra-cache','true'),('--no-record-state-extra-cache','false')]:
            command=self.launch_command(['--record-state-cache',option])
            index=command.index('record_state_extra_cache')
            self.assertEqual(command[index-1:index+2],['--ez','record_state_extra_cache',value])
        for argv in [ ['--attach','--record-state-extra-cache'],
                      ['--attach','--no-record-state-extra-cache'],
                      ['--record-state-extra-cache'],
                      ['--no-record-state-cache','--record-state-extra-cache'] ]:
            with patch('sys.argv',[str(LAUNCHER),*argv]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2)
            adb.assert_not_called()

    def test_sparse_submission_plan_is_explicit_default_off_and_cold(self):
        self.assertNotIn('sparse_submission_plan', self.launch_command([]))
        for option,value in [('--sparse-submission-plan','true'),('--no-sparse-submission-plan','false')]:
            command=self.launch_command([option]);index=command.index('sparse_submission_plan')
            self.assertEqual(command[index-1:index+2],['--ez','sparse_submission_plan',value])
            with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2)
            adb.assert_not_called()

    def test_guest_wait_budget_is_bounded_and_cold(self):
        self.assertNotIn('guest_wait_budget_us',self.launch_command([]))
        for value in ('0','30','100'):
            command=self.launch_command(['--guest-wait-budget-us',value])
            index=command.index('guest_wait_budget_us')
            self.assertEqual(command[index-1:index+2],['--ei','guest_wait_budget_us',value])
        for arguments in (['--guest-wait-budget-us','-1'],['--guest-wait-budget-us','101'],
                          ['--attach','--guest-wait-budget-us','30']):
            with patch('sys.argv',[str(LAUNCHER),*arguments]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2)
            adb.assert_not_called()

    def test_swapchain_ubwc_requires_explicit_cold_custom_provider(self):
        self.assertNotIn('turnip_swapchain_ubwc',self.launch_command([]))
        for option,value in [('--turnip-swapchain-ubwc','true'),('--no-turnip-swapchain-ubwc','false')]:
            command=self.launch_command(['--vulkan-provider','adreno',option])
            index=command.index('turnip_swapchain_ubwc')
            self.assertEqual(command[index-1:index+2],['--ez','turnip_swapchain_ubwc',value])
            for invalid in ([option],['--vulkan-provider','system',option],['--attach',option]):
                with patch('sys.argv',[str(LAUNCHER),*invalid]), \
                        patch('subprocess.check_output') as adb, \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    runpy.run_path(str(LAUNCHER),run_name='__main__')
                self.assertEqual(caught.exception.code,2)
                adb.assert_not_called()
    def test_query_attachment_reference_is_explicit_and_cold_only(self):
        self.assertNotIn('query_attachment_reference',self.launch_command([]))
        for option,value in [('--query-attachment-reference','true'),('--no-query-attachment-reference','false')]:
            command=self.launch_command([option]);index=command.index('query_attachment_reference')
            self.assertEqual(command[index-1:index+2],['--ez','query_attachment_reference',value])
            with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2)
            adb.assert_not_called()
    def test_native_buffer_geometry_is_explicit_and_cold_only(self):
        self.assertNotIn('native_buffer_geometry',self.launch_command([]))
        for option,value in [('--native-buffer-geometry','true'),('--no-native-buffer-geometry','false')]:
            command=self.launch_command([option]);index=command.index('native_buffer_geometry')
            self.assertEqual(command[index-1:index+2],['--ez','native_buffer_geometry',value])
            with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2)
            adb.assert_not_called()
    def test_guest_worker_qos_is_explicit_and_cold_only(self):
        self.assertNotIn('guest_worker_qos',self.launch_command([]))
        for option,value in [('--guest-worker-qos','true'),('--no-guest-worker-qos','false')]:
            command=self.launch_command([option]);index=command.index('guest_worker_qos')
            self.assertEqual(command[index-1:index+2],['--ez','guest_worker_qos',value])
            with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2)
            adb.assert_not_called()
    def test_fixed_recording_arms_have_explicit_matched_prerequisites(self):
        self.assertNotIn('render_record_mode',self.launch_command([]))
        for mode,value in [('secondary','1'),('primary','2')]:
            command=self.launch_command(['--fixed-render-recording',mode,
                                         '--keep-attachment-stores','--no-pixel-local'])
            index=command.index('render_record_mode')
            self.assertEqual(command[index-1:index+2],['--ei','render_record_mode',value])
        bad=[['--fixed-render-recording','primary'],
             ['--fixed-render-recording','primary','--keep-attachment-stores'],
             ['--fixed-render-recording','primary','--no-pixel-local'],
             ['--attach','--fixed-render-recording','primary','--keep-attachment-stores','--no-pixel-local'],
             ['--fixed-render-recording','secondary','--keep-attachment-stores','--no-pixel-local','--deferred-mip-clears']]
        for options in bad:
            with self.subTest(options=options),patch('sys.argv',[str(LAUNCHER),*options]), \
                    patch('subprocess.check_output')as adb,contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit)as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2);adb.assert_not_called()

    def test_keep_stores_is_explicit_cold_only_and_default_off(self):
        for options,value in (([], 'false'), (['--keep-attachment-stores'], 'true')):
            command=self.launch_command(options)
            index=command.index('keep_attachment_stores')
            self.assertEqual(command[index-1:index+2],['--ez','keep_attachment_stores',value])
        with patch('sys.argv',[str(LAUNCHER),'--attach','--keep-attachment-stores']), \
                patch('subprocess.check_output') as adb, \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
            runpy.run_path(str(LAUNCHER),run_name='__main__')
        self.assertEqual(caught.exception.code,2)
        adb.assert_not_called()

    def test_planning_reference_is_cold_and_explicit(self):
        command=self.launch_command([])
        index=command.index('submission_plan_reference')
        self.assertEqual(command[index-1:index+2],['--ez','submission_plan_reference','false'])
        command=self.launch_command(['--submission-plan-reference'])
        index=command.index('submission_plan_reference')
        self.assertEqual(command[index-1:index+2],['--ez','submission_plan_reference','true'])
        with patch('sys.argv',[str(LAUNCHER),'--attach','--submission-plan-reference']), \
                patch('subprocess.check_output') as adb, \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
            runpy.run_path(str(LAUNCHER),run_name='__main__')
        self.assertEqual(caught.exception.code,2)
        adb.assert_not_called()

    def launch_command(self, options):
        def adb(command, **kwargs):
            if 'am' in command and 'start' in command:
                raise LaunchCaptured(command)
            return ''

        with patch('sys.argv', [str(LAUNCHER), *options]), \
                patch('subprocess.check_output', side_effect=adb), \
                patch('time.sleep'), self.assertRaises(LaunchCaptured) as caught:
            runpy.run_path(str(LAUNCHER), run_name='__main__')
        return caught.exception.command

    def test_native_read_group_front_is_explicit_and_cannot_change_running_experiment(self):
        self.assertNotIn('native_read_group_front', self.launch_command([]))
        for option,value in (('--native-read-group-front','true'),
                             ('--no-native-read-group-front','false')):
            with self.subTest(option=option):
                command=self.launch_command([option])
                index=command.index('native_read_group_front')
                self.assertEqual(command[index-1:index+2],['--ez','native_read_group_front',value])
                with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                        patch('subprocess.check_output') as adb, \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    runpy.run_path(str(LAUNCHER),run_name='__main__')
                self.assertEqual(caught.exception.code,2)
                adb.assert_not_called()

    def test_existing_user_game_is_never_stopped_or_relaunched(self):
        for appeared_after_wakeup in (False, True):
            commands = []; queries = []
            def adb(command, **kwargs):
                commands.append(command)
                if 'pidof ' in ' '.join(command):
                    queries.append(command)
                    return '' if appeared_after_wakeup and len(queries) == 1 else '12345\n'
                return ''
            with self.subTest(appeared_after_wakeup=appeared_after_wakeup), \
                    patch('sys.argv', [str(LAUNCHER)]), \
                    patch('subprocess.check_output', side_effect=adb), \
                    patch('time.sleep'), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER), run_name='__main__')
            self.assertIn('left untouched', str(caught.exception))
            self.assertFalse(any('force-stop' in command or 'start' in command for command in commands))
            self.assertEqual(len(queries), 2 if appeared_after_wakeup else 1)

    def test_default_leaves_native_startup_default_unchanged(self):
        self.assertNotIn('pool_window_bytes', self.launch_command([]))

    def test_state_segments_are_explicit_and_cannot_change_running_experiment(self):
        self.assertNotIn('state_segments', self.launch_command([]))
        for option,value in (('--state-segments','true'),
                             ('--no-state-segments','false')):
            with self.subTest(option=option):
                command=self.launch_command([option])
                index=command.index('state_segments')
                self.assertEqual(command[index-1:index+2],['--ez','state_segments',value])
                with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                        patch('subprocess.check_output') as adb, \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    runpy.run_path(str(LAUNCHER),run_name='__main__')
                self.assertEqual(caught.exception.code,2)
                adb.assert_not_called()

    def test_attachment_job_reuse_is_explicit_and_cannot_change_running_experiment(self):
        self.assertNotIn('attachment_job_reuse', self.launch_command([]))
        for option,value in (('--attachment-job-reuse','true'),
                             ('--no-attachment-job-reuse','false')):
            with self.subTest(option=option):
                command=self.launch_command([option])
                index=command.index('attachment_job_reuse')
                self.assertEqual(command[index-1:index+2],['--ez','attachment_job_reuse',value])
                with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                        patch('subprocess.check_output') as adb, \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    runpy.run_path(str(LAUNCHER),run_name='__main__')
                self.assertEqual(caught.exception.code,2)
                adb.assert_not_called()

    def test_attachment_raster_run_is_explicit_and_cannot_change_running_experiment(self):
        self.assertNotIn('attachment_raster_run', self.launch_command([]))
        for option,value in (('--attachment-raster-run','true'),
                             ('--no-attachment-raster-run','false')):
            with self.subTest(option=option):
                command=self.launch_command([option])
                index=command.index('attachment_raster_run')
                self.assertEqual(command[index-1:index+2],['--ez','attachment_raster_run',value])
                with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                        patch('subprocess.check_output') as adb, \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    runpy.run_path(str(LAUNCHER),run_name='__main__')
                self.assertEqual(caught.exception.code,2)
                adb.assert_not_called()

    def test_buffer_plan_is_explicit_and_cannot_change_running_experiment(self):
        self.assertNotIn('buffer_consumption_plan', self.launch_command([]))
        for option,value in (('--buffer-consumption-plan','true'),
                             ('--no-buffer-consumption-plan','false')):
            with self.subTest(option=option):
                command=self.launch_command([option])
                index=command.index('buffer_consumption_plan')
                self.assertEqual(command[index-1:index+2],['--ez','buffer_consumption_plan',value])
                with patch('sys.argv',[str(LAUNCHER),'--attach',option]), \
                        patch('subprocess.check_output') as adb, \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    runpy.run_path(str(LAUNCHER),run_name='__main__')
                self.assertEqual(caught.exception.code,2)
                adb.assert_not_called()

    def test_exact_long_extra_including_above_double_precision(self):
        for size in (0, 4096, 1 << 20, 1 << 62):
            with self.subTest(size=size):
                command = self.launch_command(['--pool-window-bytes', str(size)])
                index = command.index('pool_window_bytes')
                self.assertEqual(command[index - 1:index + 2], ['--el', 'pool_window_bytes', str(size)])

    def test_invalid_sizes_fail_before_any_adb(self):
        for value in ('-1', '1', '4095', '4097', '8193', str(1 << 63), '4096.0', '1e6'):
            with self.subTest(value=value), \
                    patch('sys.argv', [str(LAUNCHER), '--pool-window-bytes', value]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER), run_name='__main__')
            self.assertEqual(caught.exception.code, 2)
            adb.assert_not_called()

    def test_attach_cannot_reconfigure_even_whole_pool(self):
        with patch('sys.argv', [str(LAUNCHER), '--attach', '--pool-window-bytes', '0']), \
                patch('subprocess.check_output') as adb, \
                contextlib.redirect_stderr(io.StringIO()) as stderr, \
                self.assertRaises(SystemExit) as caught:
            runpy.run_path(str(LAUNCHER), run_name='__main__')
        self.assertEqual(caught.exception.code, 2)
        self.assertIn('startup-only', stderr.getvalue())
        adb.assert_not_called()

    def test_attach_cannot_change_pixel_local_module_identity(self):
        for option in ('--pixel-local', '--no-pixel-local'):
            with self.subTest(option=option), \
                    patch('sys.argv', [str(LAUNCHER), '--attach', option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()) as stderr, \
                    self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER), run_name='__main__')
            self.assertEqual(caught.exception.code, 2)
            self.assertIn('startup-only', stderr.getvalue())
            adb.assert_not_called()

    def test_pixel_local_default_is_delegated_and_reference_is_explicit(self):
        self.assertNotIn('pixel_local', self.launch_command([]))
        for option, value in (('--pixel-local', 'true'), ('--no-pixel-local', 'false')):
            with self.subTest(option=option):
                command = self.launch_command([option])
                index = command.index('pixel_local')
                self.assertEqual(command[index - 1:index + 2], ['--ez', 'pixel_local', value])

    def test_copy_accounting_is_startup_only_and_default_off(self):
        for options, value in (([], 'false'), (['--pool-copy-accounting'], 'true'),
                               (['--no-pool-copy-accounting'], 'false')):
            command = self.launch_command(options)
            index = command.index('pool_copy_accounting')
            self.assertEqual(command[index - 1:index + 2], ['--ez', 'pool_copy_accounting', value])
        for option in ('--pool-copy-accounting', '--no-pool-copy-accounting'):
            with patch('sys.argv', [str(LAUNCHER), '--attach', option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER), run_name='__main__')
            self.assertEqual(caught.exception.code, 2)
            adb.assert_not_called()

    def test_dual_upload_is_an_exact_startup_boolean(self):
        for options, value in (([], 'false'), (['--upload-dual-store'], 'true'),
                               (['--no-upload-dual-store'], 'false')):
            with self.subTest(options=options):
                command = self.launch_command(options)
                index = command.index('upload_dual_store')
                self.assertEqual(command[index - 1:index + 2], ['--ez', 'upload_dual_store', value])

    def test_attach_cannot_change_dual_upload_policy(self):
        for option in ('--upload-dual-store', '--no-upload-dual-store'):
            with self.subTest(option=option), \
                    patch('sys.argv', [str(LAUNCHER), '--attach', option]), \
                    patch('subprocess.check_output') as adb, \
                    contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER), run_name='__main__')
            self.assertEqual(caught.exception.code, 2)
            adb.assert_not_called()

    def test_shared_heap_and_provider_are_explicit_cold_requests(self):
        self.assertNotIn('shared_guest_heap', self.launch_command([]))
        for option,value in (('--shared-guest-heap','true'),('--no-shared-guest-heap','false')):
            command=self.launch_command([option,'--vulkan-provider','panvk'])
            i=command.index('shared_guest_heap');self.assertEqual(command[i-1:i+2],['--ez','shared_guest_heap',value])
            i=command.index('vulkan_provider');self.assertEqual(command[i-1:i+2],['--es','vulkan_provider','panvk'])
        for options in (['--attach','--shared-guest-heap'],['--attach','--no-shared-guest-heap'],
                        ['--attach','--vulkan-provider','panvk'],['--attach','--custom-vulkan-driver'],
                        ['--vulkan-provider','panvk','--validate'],
                        ['--vulkan-provider','panvk','--custom-vulkan-driver']):
            with self.subTest(options=options),patch('sys.argv',[str(LAUNCHER),*options]), \
                    patch('subprocess.check_output') as adb,contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit) as caught:
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            self.assertEqual(caught.exception.code,2);adb.assert_not_called()


if __name__ == '__main__':
    unittest.main()
