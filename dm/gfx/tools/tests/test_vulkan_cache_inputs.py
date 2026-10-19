#!/usr/bin/env python3
"""Reject stale shader products when only immutable c1 input bytes change."""
import hashlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from dmsh_vulkan import cache_inputs_match


class VulkanCacheInputs(unittest.TestCase):
    def test_same_code_and_control_different_c1(self):
        # A control key and instruction checksum alone cannot distinguish these
        # inputs. A change beyond the first constants must also miss the cache.
        instructions=b'unchanged shader instructions'
        c1=bytes(767)+b'\x01'
        code_hash=hashlib.sha256(instructions).hexdigest()
        cached={'key':'unchanged-control-key','chain':'same-toolchain',
                'source_sha256':code_hash,'c1':c1.hex()}
        self.assertTrue(cache_inputs_match(cached,'same-toolchain',code_hash,c1))
        changed=c1[:-1]+b'\x02'
        self.assertFalse(cache_inputs_match(cached,'same-toolchain',code_hash,changed))
        self.assertFalse(cache_inputs_match(cached,'same-toolchain',code_hash,c1[:-1]))
        self.assertFalse(cache_inputs_match(cached,'new-toolchain',code_hash,c1))
        self.assertFalse(cache_inputs_match(cached,'same-toolchain','changed-code',c1))

    def test_complete_bit_patterns_and_missing_legacy_input(self):
        # Compare original bytes, retaining signed zero and NaN payloads.
        c1=bytes.fromhex('000000803412c07f')
        cached={'chain':'chain','source_sha256':'code','c1':c1.hex()}
        self.assertTrue(cache_inputs_match(cached,'chain','code',c1))
        for changed in ['000000003412c07f','000000803512c07f']:
            self.assertFalse(cache_inputs_match(cached,'chain','code',bytes.fromhex(changed)))
        cached.pop('c1')
        self.assertFalse(cache_inputs_match(cached,'chain','code',b''))
        cached['c1']=''
        self.assertTrue(cache_inputs_match(cached,'chain','code',b''))


if __name__=='__main__':unittest.main()
