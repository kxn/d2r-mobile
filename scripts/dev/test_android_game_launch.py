#!/usr/bin/env python3
"""Check startup-only allocation selection without contacting any device."""
import contextlib
import io
import json
import re
import tempfile
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch


LAUNCHER = Path(__file__).with_name('android-game-launch.py')


class LaunchCaptured(Exception):
    def __init__(self, command):
        self.command = command


class PoolWindowLaunchTests(unittest.TestCase):
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

    def test_shared_image_raw_observe_is_explicit_cold_shared_only(self):
        self.assertNotIn('shared_image_raw_observe',self.launch_command([]))
        command=self.launch_command(['--shared-guest-heap','--shared-image-raw-observe'])
        i=command.index('shared_image_raw_observe')
        self.assertEqual(command[i-1:i+2],['--ez','shared_image_raw_observe','true'])
        for options in ([],['--no-shared-guest-heap'],['--shared-guest-heap','--attach']):
            with self.subTest(options=options),patch('sys.argv',[str(LAUNCHER),*options,'--shared-image-raw-observe']), \
                 patch('subprocess.check_output') as adb,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            adb.assert_not_called()

    def test_shared_lazy_is_explicit_cold_launch_only(self):
        self.assertNotIn('shared_lazy_windows',self.launch_command([]))
        valid=['--shared-guest-heap','--expect-shared-import-windows','--pool-window-bytes','1048576']
        command=self.launch_command(valid+['--shared-lazy-windows'])
        i=command.index('shared_lazy_windows')
        self.assertEqual(command[i-1:i+2],['--ez','shared_lazy_windows','true'])
        for options in ([],['--shared-guest-heap'],valid+['--attach'],
                        ['--shared-guest-heap','--expect-shared-import-windows','--pool-window-bytes','0']):
            with self.subTest(options=options),patch('sys.argv',[str(LAUNCHER),*options,'--shared-lazy-windows']), \
                 patch('subprocess.check_output') as adb,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                runpy.run_path(str(LAUNCHER),run_name='__main__')
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

    def observed_ready(self, options):
        # Actual completed original f75 game observations, never synthetic game
        # execution. Only ADB transport is fake; run the entire existing helper.
        root=Path('/home/dl/d2r-mobile/.local/logs/driver-proof-fingerprint-f75de42-REFERENCE1-FP-Q735-v1/native')
        native=(root/'native.log').read_text()
        pid=re.search(r'Android native start pid=(\d+)\n',native)[1]
        eligible=[]
        for line in (root/'after-route.jsonl').read_text().splitlines():
            state=json.loads(line);t=state.get('time_ns',0);binding=state.get('native_bindings',{})
            output=state.get('game_output',{});surface=state.get('surface_output',{})
            if (state.get('valid') and state.get('world_input_ready') and binding.get('ready')
                    and state.get('present_interval_ms',0)>0
                    and min(output.get('width',0),output.get('height',0),surface.get('width',0),surface.get('height',0))>0
                    and 0<=t-binding.get('captured_ns',0)<1_000_000_000
                    and 0<=t-state.get('present_sample_ns',0)<1_000_000_000):
                eligible.append(state)
        self.assertGreaterEqual(len(eligible),2)
        states=eligible[-2:];self.assertLess(states[0]['time_ns'],states[1]['time_ns'])
        self.assertTrue(all(x['guest_heap']['shared'] and x['pool_windows']['window_bytes']==1048576 for x in states))
        commands=[];reads=[]
        def adb(command, **kwargs):
            commands.append(command)
            if command[-2:]==['pidof','com.d2rmobile.nativeport']:return pid+'\n'
            if 'pidof ' in ' '.join(command):return '' # cold pre-launch shell guard only
            if command[-1].endswith('/native.log'):return native
            if command[-1].endswith('/game-state.jsonl'):
                current=states[min(len(reads),1)];reads.append(current);return json.dumps(current)+'\n'
            if command[-1].endswith('/shaders/.chain'):return '9507875007c6\n'
            if 'start' in command or 'input' in command or 'wm' in command:return ''
            raise AssertionError(('unexpected transport request',command))
        with tempfile.TemporaryDirectory() as temp:
            report=Path(temp)/'result.json'
            with patch('sys.argv',[str(LAUNCHER),*options,'--timeout','1','--report',str(report)]), \
                    patch('subprocess.check_output',side_effect=adb),patch('time.sleep'), \
                    contextlib.redirect_stdout(io.StringIO()):
                runpy.run_path(str(LAUNCHER),run_name='__main__')
            return json.loads(report.read_text()),commands

    def test_attach_observes_actual_shared_1MiB_without_requesting_cold_policy(self):
        report,commands=self.observed_ready(['--attach'])
        self.assertTrue(report['attached']);self.assertTrue(report['original_hud'])
        self.assertTrue(report['actual_guest_heap']['shared'])
        self.assertEqual(report['actual_game_state']['pool_windows']['window_bytes'],1048576)
        self.assertIsNone(report['requested_pool_window_bytes'])
        self.assertFalse(any('am' in x or 'input' in x for x in commands))

    def test_cold_shared_window_still_requires_exact_requested_policy(self):
        report,_=self.observed_ready(['--shared-guest-heap','--pool-copy-accounting','--expect-shared-import-windows','--pool-window-bytes','1048576'])
        self.assertTrue(report['original_hud']);self.assertFalse(report['attached'])
        with self.assertRaisesRegex(SystemExit,'pool backing window policy'):
            self.observed_ready(['--shared-guest-heap','--pool-copy-accounting','--expect-shared-import-windows','--pool-window-bytes','16777216'])
        with self.assertRaisesRegex(RuntimeError,'fixed whole-pool policy'):
            self.observed_ready(['--shared-guest-heap','--pool-copy-accounting'])

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
