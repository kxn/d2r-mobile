import importlib.util
from pathlib import Path
import tempfile
import unittest
def load(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
MAP=load('map_sources');SELECT=load('select_complete')
class SourceSelection(unittest.TestCase):
    def test_source_blake3_uint32_little_endian_not_native_hash(self):
        self.assertEqual(MAP.decode_source_blake3([0x1e3cd7e8,0x7d1b4056,0xa93ed027,0x177ee325,0x56888822,0x2919e112,0x9b9f98df,0x9eb73dba]),'e8d73c1e56401b7d27d03ea925e37e172288885612e11929df989f9bba3db79e')
        with self.assertRaises(ValueError):MAP.decode_source_blake3([0]*7)
        with self.assertRaises(ValueError):MAP.decode_source_blake3([0]*7+[2**32])
    def file(self,text):
        t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);p=Path(t.name)/'input.csv';p.write_text(text);return p
    def test_complete_requested_structure_is_bounded_not_latest_longest(self):
        p=self.file('frame,batch,time_ns,event,\n0,1,1,start_cmd_buffer,\n0,1,5,end_cmd_buffer,renderpasses=49\n0,2,6,start_cmd_buffer,\n0,2,9,end_cmd_buffer,renderpasses=0\n0,3,10,start_cmd_buffer,\n0,3,20,end_cmd_buffer,renderpasses=49\n0,4,21,start_cmd_buffer,\n')
        rows,proof=SELECT.select(p,49,1);self.assertEqual([r['batch']for r in rows],[3]);self.assertEqual(proof['batch_blocks_with_missing_CB_edges'],1);self.assertTrue(proof['selection_is_not_a_performance_window'])
    def test_interleaved_batches_are_rejected(self):
        p=self.file('0,2,1,start_cmd_buffer,\n0,1,2,end_cmd_buffer,renderpasses=49\n')
        with self.assertRaises(ValueError):SELECT.select(p,49,3)
if __name__=='__main__':unittest.main(verbosity=2)
