"""Bounded saved-byte provenance cases, no target process or device."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

p = Path(__file__).resolve().parents[1] / 'sdk_nvn_pool_registry.py'
s = importlib.util.spec_from_file_location('arena_metadata_test', p)
m = importlib.util.module_from_spec(s)
s.loader.exec_module(m)
R = Path('/home/dl/d2r-mobile')


class SavedSnapshot:
    def __init__(self):
        self.config = dict(elf=dict(sha256=m.ELF_930_SHA, buildid=m.BUILD_930_ID))
        self.files, self.calls = [], []
        self.flat = (R / '.local/logs/SDK-free-span-readonly-v1/original-main-flat.bin').read_bytes()
        self.base, self.primary, self.alias, self.current = 0x7000000000, 0x8000000000, 0x9000000000, 1 << 30
        self.data, self.spans = {}, []
        self.pp, self.ap = bytearray(self.current // 4096 * 8), bytearray(self.current // 4096 * 8)
        for off, length, _ in m.CODE_PROOFS:
            self.data[off] = self.flat[off:off+length]
        for off, text in m.STRING_PROOFS:
            self.data[off] = text
        for spec, off in zip(m.ARENA_RECORDS, [0x1000, 0x10001000, 0x28001000]):
            ptr = self.primary + off
            self.data[spec[1]] = struct.pack('<QQQQ', 0xa000, ptr, spec[2], ptr)
            self.spans.append(dict(state=1, begin=ptr, end=ptr+spec[2], pages=spec[2]//4096))
            if spec[3]: self.data[spec[3]] = struct.pack('<Q', self.base+spec[4])
            struct.pack_into('<Q', self.pp, off//4096*8, 1 << 63)
            struct.pack_into('<Q', self.ap, off//4096*8, 1 << 63)
        self.data[0x31164a0] = struct.pack('<Q', 0xfa00000)
        self.mutate_after = None

    def check(self): pass

    def read(self, address, size, name):
        off = address - self.base
        raw = self.data[off]
        if self.mutate_after == off and '-after' in name: raw = bytes(len(raw))
        if len(raw) != size: raise ValueError('fixture short read')
        self.files.append(dict(address=address, bytes=size, name=name))
        self.calls.append((address, size))
        return raw

    def capture(self):
        nvn = dict(complete=True, mutex_mode=m.MODE_930_Q735, libc_sha256=m.Q735_LIBC_SHA,
                   pools=[dict(identity=5, guest=self.primary+0x10001000, size=4096)])
        with patch.object(m, 'validate_main_arena_pin'):
            return m.capture_main_arenas(None, self, {}, dict(base=self.base, bytes=len(self.flat)),
                dict(g_heap=self.primary, g_heap_import_alias=self.alias, g_heap_cur=self.current),
                self.spans, 4096, self.pp, self.ap, nvn)


class ArenaTests(unittest.TestCase):
    def test_three_records_same_stop_union_and_fixed_budget(self):
        f=SavedSnapshot();r=f.capture()
        self.assertTrue(r['complete'], r['errors'])
        self.assertEqual(len(r['records']),3)
        self.assertEqual(r['metadata_acquisition_bytes'],810)
        self.assertEqual(sum(n for _,n in f.calls),810)
        self.assertEqual(r['records'][1]['present_cpu_views']['union_bytes'],4096)
        self.assertEqual(r['records'][1]['current_registered_Pool_intersections'][0]['bytes'],4096)

    def test_alias_pointer_correlates_same_backing_not_second_allocation(self):
        f=SavedSnapshot();spec=m.ARENA_RECORDS[1]
        raw=struct.pack('<QQQQ',0xa000,f.alias+0x10001000,spec[2],f.alias+0x10001000)
        f.data[spec[1]]=raw;r=f.capture()
        self.assertTrue(r['complete'],r['errors'])
        self.assertEqual(r['records'][1]['source_alias'],'import_alias')
        self.assertEqual(r['records'][1]['primary_begin'],f.primary+0x10001000)
        self.assertEqual(r['records'][1]['present_cpu_views']['union_bytes'],4096)

    def test_free_span_or_wrong_record_is_unknown_not_reclaimable(self):
        for kind in ['freed','zero','size','outside','control']:
            with self.subTest(kind=kind):
                f=SavedSnapshot();spec=m.ARENA_RECORDS[0];values=list(struct.unpack('<QQQQ',f.data[spec[1]]))
                if kind=='freed': f.spans[0]['state']=2
                elif kind=='zero': values[0]=0
                elif kind=='size': values[2]+=4096
                elif kind=='outside': values[1]=1<<60;values[3]=values[1]
                else: values[3]=values[1]+values[2]
                f.data[spec[1]]=struct.pack('<QQQQ',*values)
                r=f.capture();self.assertFalse(r['complete']);self.assertTrue(r['errors'])

    def test_original_code_string_and_label_must_all_match(self):
        for off in [m.CODE_PROOFS[0][0],m.STRING_PROOFS[0][0],m.ARENA_RECORDS[1][3]]:
            with self.subTest(offset=off):
                f=SavedSnapshot();raw=bytearray(f.data[off]);raw[0]^=1;f.data[off]=bytes(raw)
                r=f.capture();self.assertFalse(r['complete']);self.assertTrue(r['errors'])

    def test_mid_lifecycle_after_record_is_not_success(self):
        f=SavedSnapshot();f.mutate_after=m.ARENA_RECORDS[2][1];r=f.capture()
        self.assertFalse(r['complete']);self.assertIn('changed at all-stop',r['errors'][0])

    def test_actual_local_input_pin_and_corrupt_manifest(self):
        import tempfile,json
        folder=R/'.local/logs/shared-b2t-ordered-preflight-930ac8f-raw-nso-ready-agent.json'
        paths=dict(main_original=R/'work/data/switch/3.3.94012/Update/ExeFS/main',
                   main_flat=R/'.local/logs/SDK-free-span-readonly-v1/original-main-flat.bin',
                   qualified_main=R/'.local/build/native-touch-x18-shared-b2t-ordered-preflight-930ac8f/main.patched.bin',
                   raw_ready=folder)
        pin=dict(enabled=True,mode=m.ARENA_MODE,**{k:dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for k,p in paths.items()})
        m.validate_main_arena_pin(pin)
        bad=copy.deepcopy(pin);bad['main_original']['sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'hash changed'): m.validate_main_arena_pin(bad)
        with tempfile.TemporaryDirectory() as td:
            q=Path(td)/'bad.json';d=json.loads(folder.read_text());d['source_commit']='0'*40;q.write_text(json.dumps(d))
            bad=copy.deepcopy(pin);bad['raw_ready']=dict(path=str(q),sha256=hashlib.sha256(q.read_bytes()).hexdigest())
            with self.assertRaisesRegex(ValueError,'producer/verify-only'):m.validate_main_arena_pin(bad)

    def test_930_mutex_mode_is_exact_old_state16_rule(self):
        m.normal_resources_unlocked(bytes.fromhex('0000161f'+'00'*36),m.MODE_930_Q735)
        with self.assertRaises(ValueError):m.normal_resources_unlocked(bytes.fromhex('0100161f'+'00'*36),m.MODE_930_Q735)


if __name__=='__main__': unittest.main(verbosity=2)
