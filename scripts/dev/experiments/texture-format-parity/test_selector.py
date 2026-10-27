#!/usr/bin/env python3
"""Compile the exact selector from the diagnostic patch, without an Android build."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
PATCH = Path(__file__).with_name('upstream-overlay-select.patch')


@unittest.skipUnless(shutil.which('cc'), 'host C compiler unavailable')
class Selector(unittest.TestCase):
    def test_applied_upstream_function_path_boundaries(self):
        relative = Path('dm/android/jni_android.c')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / relative.parent).mkdir(parents=True)
            original = subprocess.check_output(['git', 'show', 'b811b90:' + str(relative)], cwd=ROOT)
            (root / relative).write_bytes(original)
            subprocess.run(['git', 'apply', '--check', str(PATCH)], cwd=root, check=True)
            subprocess.run(['git', 'apply', str(PATCH)], cwd=root, check=True)
            patched = (root / relative).read_text()
            start = patched.index('static int diagnostic_texture_overlay(')
            end = patched.index('\nJNIEXPORT jint', start)
            harness = '''#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static char g_files[1024];
''' + patched[start:end] + '''
int main(int argc, char **argv) {
 if (argc < 3) return 4;
 snprintf(g_files, sizeof g_files, "%s", argv[1]);
 if (argc > 3) setenv("DM_DIAG_TEXTURE_OVERLAY", argv[3], 1);
 else unsetenv("DM_DIAG_TEXTURE_OVERLAY");
 char target[1300] = "default";
 int result = diagnostic_texture_overlay(target, (size_t)atoi(argv[2]));
 printf("%d:%s\\n", result, target);
 return 0;
}
'''
            source = root / 'selector.c'; source.write_text(harness)
            executable = root / 'selector'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
            private = root / 'private'; candidate = private / 'texture-format-parity/candidate'; candidate.mkdir(parents=True)
            outside = root / 'outside'; outside.mkdir()
            escape = candidate.parent / 'escape'; escape.symlink_to(outside)
            cases = [(None, 1300, '1:default'), (str(candidate), 1300, '1:' + str(candidate)),
                     ('', 1300, '0:default'), (str(candidate.parent), 1300, '0:default'),
                     (str(outside), 1300, '0:default'), (str(escape), 1300, '0:default'),
                     (str(private / 'texture-format-parity-else/candidate'), 1300, '0:default'),
                     (str(candidate), 8, '0:default')]
            for argument, capacity, expected in cases:
                with self.subTest(argument=argument, capacity=capacity):
                    argv = [str(executable), str(private), str(capacity)] + ([] if argument is None else [argument])
                    self.assertEqual(subprocess.check_output(argv, text=True).strip(), expected)
            self.assertNotIn('jni_android.c', (ROOT / 'scripts/dev/experiments/upstream-driver-control-v1.patch').read_text())


if __name__ == '__main__': unittest.main()
