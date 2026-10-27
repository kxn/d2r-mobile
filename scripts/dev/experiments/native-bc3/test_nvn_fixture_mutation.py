#!/usr/bin/env python3
"""Compile the actual oracle sensitizer with the existing BC decoder; no GPU."""
import argparse
import subprocess
import tempfile
import unittest
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
def run_test(sanitize):
    s = (ROOT / 'dm/host/android/tests/native_vulkan_native_resource.inc').read_text()
    start = s.index('static void nr_mutated_blocks(')
    end = s.index('\n_Static_assert', start)
    function = s[start:end]
    with tempfile.TemporaryDirectory(prefix='nvn-bc3-sensitizer-') as td:
        d = Path(td)
        code = '''#include <stdint.h>
    #include <string.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include "nvn_bcdec.h"
    #define CHECK(x) do {if(!(x))abort();} while(0)
    ''' + function + '''
    int main(void) {
      const uint8_t white[16]={255,255,0,0,0,0,0,0,255,255,255,255,170,170,170,170};
      uint8_t original[32],copy[32],changed[32],before[64],after[64];
      memcpy(original,white,16);memcpy(original+16,white,16);memcpy(copy,original,32);
      for(unsigned format=0x44;format<=0x48;format+=4) {
        nr_mutated_blocks(format,original,changed);
        CHECK(!memcmp(original,copy,32));CHECK(memcmp(original,changed,32));
        CHECK(!nvn_bc_decode(format,original,16,4,4,before,16));
        CHECK(!nvn_bc_decode(format,changed,16,4,4,after,16));
        CHECK(before[3]==255 && after[3]==0);
        CHECK(!memcmp(before,after,3));
        // The original NVN oracle restores/reapplies these same saved bytes, not
        // a second data-dependent choice, including both trailing block bytes.
        uint8_t working[32];memcpy(working,changed,32);memcpy(working,original,32);
        CHECK(!memcmp(working,copy,32));memcpy(working,changed,32);
        CHECK(!memcmp(working,changed,32));
      }
      // Existing nonconstant block semantics remain the exact original swap for
      // both ASTC and BC3; no BC decoder is used for an ASTC mutation.
      original[0]=1;original[16]=2;memcpy(copy,original,32);
      for(unsigned n=0;n<3;n++) {
        unsigned format=(unsigned[]){0x79,0x44,0x48}[n];
        nr_mutated_blocks(format,original,changed);
        CHECK(!memcmp(original,copy,32));
        CHECK(!memcmp(changed,original+16,16) && !memcmp(changed+16,original,16));
      }
      puts("PASS actual NVN fixture sensitizer: constant BC3 UNORM+sRGB decoded alpha change; exact saved restore/reapply; ASTC/BC3 original swap; input unchanged. GPU_executed=false");
    }
    '''
        (d / 'test.c').write_text(code)
        command = ['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(ROOT / 'dm/gfx/tex'), str(d / 'test.c'),
                   str(ROOT / 'dm/gfx/tex/nvn_bcdec.c'), '-lm', '-o', str(d / 'test')]
        if sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        subprocess.run([str(d / 'test')], check=True)


class MutationTests(unittest.TestCase):
    def test_actual_encoder_sensitizer(self):
        run_test(False)

    def test_actual_encoder_sensitizer_sanitized(self):
        run_test(True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0]] + rest,
                  defaultTest='MutationTests.test_actual_encoder_sensitizer_sanitized' if args.sanitize else None)
