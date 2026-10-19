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

    def test_default_leaves_native_startup_default_unchanged(self):
        self.assertNotIn('pool_window_bytes', self.launch_command([]))

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


if __name__ == '__main__':
    unittest.main()
