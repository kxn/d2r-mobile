from pathlib import Path
import importlib.util,json,sqlite3,tempfile,unittest,zipfile
from unittest.mock import patch,Mock
D=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('sdp',D/'sdp_realtime.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
def database(path,missing=None,zero=None,nan=None,gap=False):
 c=sqlite3.connect(path);c.executescript('CREATE TABLE Metric(id INTEGER,name TEXT,description TEXT,global INTEGER);CREATE TABLE tblDataLog(metric INTEGER,timestamp INTEGER,valueID INTEGER);CREATE TABLE tblMetricDouble(value REAL);')
 for i,n in enumerate(s.REQUIRED):
  if n==missing:continue
  c.execute('INSERT INTO Metric VALUES(?,?,?,1)',(i,n,'official unit definition '+n))
  for k in range(241):
   if gap and k in range(30,60):continue
   v=0 if n==zero else (float('inf')if n==nan else 10)
   cursor=c.execute('INSERT INTO tblMetricDouble VALUES(?)',(v,));c.execute('INSERT INTO tblDataLog VALUES(?,?,?)',(i,1_000_000+k*50_000,cursor.lastrowid))
 c.commit();c.close()
class SDP(unittest.TestCase):
 def test_real_sqlite_required_definitions_and_raw_units(self):
  with tempfile.TemporaryDirectory()as d:
   p=Path(d);database(p/'x');r=s.metrics(p/'x',1_000_000,13_000_000,p)
   self.assertEqual(set(r),set(s.REQUIRED));self.assertEqual(r[s.REQUIRED[0]]['samples'],241)
   self.assertIn('official unit definition',(p/'official-counter-samples.csv').read_text())
 def test_missing_zero_rate_nonfinite_and_gap_refused(self):
  for kwargs in [{'missing':s.REQUIRED[3]},{'zero':s.REQUIRED[3]},{'nan':s.REQUIRED[0]},{'gap':True}]:
   with self.subTest(kwargs=kwargs),tempfile.TemporaryDirectory()as d:
    p=Path(d)/'x';database(p,**kwargs)
    with self.assertRaises(RuntimeError):s.metrics(p,1_000_000,13_000_000)
 def test_zero_cache_miss_is_valid_not_missing(self):
  with tempfile.TemporaryDirectory()as d:
   p=Path(d)/'x';database(p,zero='% Texture L2 Miss');self.assertEqual(s.metrics(p,1_000_000,13_000_000)['% Texture L2 Miss']['max'],0)
 def test_archive_whitelist_and_original_members(self):
  with tempfile.TemporaryDirectory()as d:
   p=Path(d);z=p/'a.sdp'
   with zipfile.ZipFile(z,'w')as f:f.writestr('sdp.db',b'official');f.writestr('version.txt',b'2026.9')
   self.assertEqual(s.saved_database(z,p).read_bytes(),b'official')
   with zipfile.ZipFile(z,'a')as f:f.writestr('../escape',b'bad')
   with self.assertRaisesRegex(RuntimeError,'members'):s.saved_database(z,p)
 def test_replacement_host_and_remote_preserved_on_cleanup(self):
  c={'gui':{'pid':1},'xvfb':{'pid':2},'collectors':[{'pid':3}]}
  with patch.object(s,'host_identity',return_value={'pid':999}),patch.object(s,'remote_identity',return_value={'pid':999}),patch.object(s,'ad')as ad,patch.object(s.os,'kill')as kill,patch.object(s.time,'sleep'):
   errors=s.close(c);self.assertTrue(errors);ad.assert_not_called();kill.assert_not_called()
 def test_owners_refused_before_UI(self):
  c={'gui':{'pid':1},'xvfb':{'pid':2},'collectors':[]}
  with patch.object(s,'host_identity',return_value=None),self.assertRaisesRegex(RuntimeError,'changed'):s.check_owners(c)
 def test_no_device_import_and_default_OFF_hook_order(self):
  spec=importlib.util.spec_from_file_location('pair_sdp_offline',D/'run-game.py');m=importlib.util.module_from_spec(spec)
  with patch('subprocess.run',side_effect=AssertionError('device action on import')),patch('subprocess.check_output',side_effect=AssertionError('device action on import')):spec.loader.exec_module(m)
  text=(D/'run-game.py').read_text();start=text.index('        m.original_route(');self.assertLess(text.index("        if p.get('performance_tools'):",start),text.index("        if p.get('official_gpu_tools') and not p.get('_official_gpu_disabled'):",start));self.assertLess(text.index("        if p.get('official_gpu_tools') and not p.get('_official_gpu_disabled'):",start),text.index("        if p.get('counter_snapshot'):",start))
 def test_actual_helper_success_sequence_with_original_archive_and_trace(self):
  state={'valid':True,'world_input_ready':True,'scale_3d':.6,'guest_heap':{'shared':True},'pool_windows':{'window_bytes':1048576},'game_output':{'width':2750,'height':1260},'surface_output':{'width':2750,'height':1260},'frame_pacing':{'target_fps':60},'pipeline_prepare':{'pipelines':2},'uniform_specialization':{'variants':3}}
  def ad(c,*a):
   if a[0]=='shell':return '123'
   if a[3]=='tail':return json.dumps(state)
   if a[3]=='cat':return 'actual Turnip maps'
   if a[3]=='sha256sum':return 'abcd  driver'
   raise AssertionError(a)
  with tempfile.TemporaryDirectory()as d:
   root=Path(d);database(root/'db')
   def save(c,p):
    with zipfile.ZipFile(p,'w')as z:z.writestr('sdp.db',(root/'db').read_bytes());z.writestr('version.txt',b'official2026.9')
   def popen(args,**kw):
    self.assertIn('--seconds',args);self.assertEqual(args[args.index('--seconds')+1],'8');self.assertIn('--frame-timeline',args)
    p=Path(args[-1]);p.write_bytes(b'original official trace');s.write(p.with_suffix('.json'),{'pid':123,'trace_sha256':s.sha(p)});return Mock(poll=lambda:0,wait=lambda **kw:0)
   c={'serial':'10CG570J59001U1','metric_definitions':{n:'official unit definition '+n for n in s.REQUIRED}}
   with patch.object(s,'ad',side_effect=ad),patch.object(s,'check_owners'),patch.object(s,'paused',return_value={'first':[0,0]}),patch.object(s,'toggle')as toggle,patch.object(s,'save_gui',side_effect=save),patch.object(s,'device_window',return_value={'epoch_us':[1_000_000,13_000_000]}),patch.object(s.subprocess,'Popen',side_effect=popen),patch.object(s.time,'sleep'),patch.object(s.time,'time_ns',side_effect=[1_000_000_000,13_000_000_000]):
    r=s.capture(c,123,root/'out',1048576,'abcd',Path('/original-helper'));self.assertTrue(r['complete']);self.assertTrue(r['gpu_validity']);self.assertEqual(toggle.call_count,2);self.assertEqual(set(r['metrics']),set(s.REQUIRED))
 def test_actual_saved_shifted_counter_block_covers_official_device8s(self):
  root=Path('/home/dl/d2r-mobile/.local/logs/native-astc-linear-6bc0b8a-sdp-game-pair-v1/control/official-gpu')
  if not (root/'sdp.db').exists():self.skipTest('owned actual tool evidence not present')
  cap=s.read(root/'official-gpu-capture.json');cover=s.fresh_coverage(root/'sdp.db',cap['paused_before']['first'][1])
  a,b=1792525905724886.2,1792525913730840.2
  self.assertLess(cover['epoch_us'][0],a);self.assertGreater(cover['epoch_us'][1],b)
  self.assertEqual(cover['per_metric'][s.REQUIRED[0]]['samples'],238)
  result=s.metrics(root/'sdp.db',a,b);self.assertGreater(result['Texture Memory Read BW (Bytes/Second)']['max'],0)
  with self.assertRaisesRegex(RuntimeError,'incomplete official coverage'):s.metrics(root/'sdp.db',cap['window_start_utc_us'],cap['window_end_utc_us'])
 def test_partial_remote_exit_snapshot_then_absent_is_clean(self):
  # One stat line while exe/status vanish during official GUI disconnect.
  tail=['S']+['0']*18+['1234'];raw='123 (sdpservice) '+' '.join(tail)
  with patch.object(s,'ad',side_effect=[raw,'']):self.assertIsNone(s.remote_identity({},123))
  with patch.object(s,'ad',side_effect=[raw,raw]),self.assertRaisesRegex(RuntimeError,'still-live'):s.remote_identity({},123)
 def test_optional_GPU_failure_actual_hook_continues_memory_path(self):
  # Execute the actual try/except hook from phase_game with a failing official
  # tool; original tool error/close recorded, no synthetic success/zero metrics.
  text=(D/'run-game.py').read_text();start=text.index("            try:\n                d['official_gpu']");end=text.index('            save()\n',start)+len('            save()\n')
  import textwrap
  code=textwrap.dedent(text[start:end]);tool=Mock();tool.capture.side_effect=RuntimeError('missing actual GPU counter');tool.close.return_value=[]
  p={'official_gpu_config':{},'pool_window_bytes':1048576,'driver':{'sha256':'same'},'official_gpu_tools':{'scheduler_helper':{}}};state={};save=Mock()
  exec(code,{'tool':tool,'p':p,'d':state,'own':'123','out':Path('/tmp'),'pinned':lambda x:Path('/original-helper'),'save':save})
  self.assertFalse(state['gpu_validity']);self.assertFalse(state['official_gpu_complete']);self.assertTrue(p['_official_gpu_disabled']);self.assertIn('missing actual GPU counter',state['official_gpu_error']);tool.close.assert_called_once();save.assert_called_once()
  self.assertIn("        require(pid() == own, 'PID changed before residency snapshot')",text[end:]);self.assertIn("(out / 'official-dumpsys-meminfo.txt').write_text",text[end:])
 def test_unpaused_unclosed_tool_skips_CPU_but_not_memory(self):
  import textwrap
  text=(D/'run-game.py').read_text();start=text.index("        if p.get('performance_tools'):",text.index('        m.original_route('));end=text.index("        if p.get('performance_tools') and not p.get('_official_gpu_unclosed'):",start)
  for errors in ([],['still live unknown collector']):
   with self.subTest(errors=errors):
    tool=Mock();tool.paused.side_effect=RuntimeError('not paused');tool.close.return_value=errors
    p={'performance_tools':{},'official_gpu_tools':{'helper':{'path':'/same-helper'}},'official_gpu_config':{}}
    p['performance_tools']={'present':True};d={};save=Mock()
    exec(textwrap.dedent(text[start:end]),{'p':p,'d':d,'module':lambda *a:tool,'save':save})
    self.assertEqual(p['_official_gpu_unclosed'],bool(errors));self.assertFalse(d['gpu_validity'])
    if errors:self.assertFalse(d['cpu_validity']);self.assertFalse(d['official_performance_complete'])
    self.assertIn("(out / 'official-dumpsys-meminfo.txt').write_text",text[end:])
 def test_unknown_metric_units_refused(self):
  with tempfile.TemporaryDirectory()as d:
   p=Path(d)/'x';database(p)
   with self.assertRaisesRegex(RuntimeError,'units'):s.metrics(p,1_000_000,13_000_000,definitions={n:'unknown unit' for n in s.REQUIRED})
 def test_actual_capture_failure_pauses_but_never_game_save_or_stop(self):
  c={'serial':'10CG570J59001U1'};state={'valid':True,'world_input_ready':True,'scale_3d':.6,'guest_heap':{'shared':True},'pool_windows':{'window_bytes':1048576},'game_output':{'width':2750,'height':1260},'surface_output':{'width':2750,'height':1260},'frame_pacing':{'target_fps':60},'pipeline_prepare':{'pipelines':2},'uniform_specialization':{'variants':3}}
  calls=[]
  def ad(c,*a):
   calls.append(a)
   if a[0]=='shell':return '123'
   if a[3]=='tail':return json.dumps(state)
   if a[3]=='cat':return 'Turnip actual maps'
   if a[3]=='sha256sum':return 'abcd  driver'
   raise AssertionError(a)
  with tempfile.TemporaryDirectory()as d,patch.object(s,'ad',side_effect=ad),patch.object(s,'check_owners'),patch.object(s,'paused',return_value={}),patch.object(s,'toggle')as toggle,patch.object(s.subprocess,'Popen',side_effect=RuntimeError('official process start fail')):
   p=Path(d)/'out'
   with self.assertRaisesRegex(RuntimeError,'start fail'):s.capture(c,123,p,1048576,'abcd',Path('/original-helper'))
   self.assertEqual(toggle.call_count,2);self.assertFalse(s.read(p/'official-gpu-capture.json')['complete'])
   self.assertFalse(any('force-stop'in str(x)or'background-save'in str(x)for x in calls))
if __name__=='__main__':unittest.main()
