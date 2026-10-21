#!/usr/bin/env python3
from pathlib import Path
import importlib.util,tempfile,unittest
D=Path(__file__).resolve().parent
s=importlib.util.spec_from_file_location('snapshot',D/'scheduling_snapshot.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def require(v,msg):
 if not v:raise RuntimeError(msg)
class Device:
 def __init__(self,names):self.names=names;self.reads=[]
 def app(self,*args):
  if args[2].startswith('for t in /proc/'):return self.names
  assert args[:2]==('sh','-c') and args[2].startswith('if test -r "$1"; then cat "$1";')
  path=args[-1];assert path.startswith('/proc/10/task/');self.reads.append(path)
  return 'UNREADABLE'if path.endswith('/sched')else 'raw existing proc fields\n'
class Snapshot(unittest.TestCase):
 def test_only_read_existing_selected_threads_and_keep_unknown_fields(self):
  d=Device('10\tbile.nativeport\n11\tbile.nativeport\n20\tnvn-translate\n30\tJit thread\n')
  with tempfile.TemporaryDirectory()as out:r=m.collect(d,'10',Path(out),require)
  self.assertEqual({x['tid']for x in r['records']},{10,11,20});self.assertEqual(len(d.reads),12)
  self.assertTrue(all(x['unreadable']for x in r['records']if x['proc_path'].endswith('/sched')))
  self.assertTrue(all('not inferred'in x['role']for x in r['records']if x['tid']==11))
 def test_missing_or_ambiguous_NvN_is_not_invented(self):
  for names in ('10\tmain\n','20\tnvn-translate\n21\tnvn-translate\n'):
   with tempfile.TemporaryDirectory()as out,self.assertRaises(RuntimeError):m.collect(Device(names),'10',Path(out),require)
if __name__=='__main__':unittest.main()
