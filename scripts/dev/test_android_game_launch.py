#!/usr/bin/env python3
"""Check startup-only allocation selection without contacting any device."""
import contextlib
import io
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
