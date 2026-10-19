#!/usr/bin/env python3
"""Capture a short primary-counter window with the unmodified Arm SDK and actual presents.

SDK binaries/data remain outside the game and repository. System GPU scope, not
exclusive game counters. The companion analyzer checks sample clocks/ring bounds.
"""
import argparse,subprocess,json,shlex,hashlib,os,time
from pathlib import Path
parser=argparse.ArgumentParser()
ROOT=Path(__file__).resolve().parents[2]
parser.add_argument('--serial',required=True);parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--deploy',action='store_true')
parser.add_argument('--arm-studio',type=Path,default=ROOT/'.local/tools/Arm_Performance_Studio_2026.5')
args=parser.parse_args();pkg='com.d2rmobile.nativeport';base=['adb','-s',args.serial]
paths={k:Path(str(args.output)+suffix) for k,suffix in [('raw','.jsonl'),('meta','.json'),('before','.sf-before.txt'),('after','.sf-after.txt'),('err','.err')]}
assert not any(p.exists() for p in paths.values());args.output.parent.mkdir(parents=True,exist_ok=True)
def adb(*parts):return subprocess.check_output(base+list(parts),text=True,timeout=20)
def state():return json.loads(adb('exec-out','run-as',pkg,'tail','-n','1','files/runtime/game-state.jsonl'))
pid=adb('shell','pidof',pkg).strip();assert pid.isdecimal()
log=adb('exec-out','run-as',pkg,'cat','files/runtime/native.log');assert f'Android native start pid={pid}\n' in log and 'external profiling: custom CPU/GPU timers disabled' in log
layer=[x for x in adb('shell','dumpsys','SurfaceFlinger','--list').splitlines() if pkg in x and 'SurfaceView[' in x and '(BLAST)' in x];assert len(layer)==1
layer=layer[0]
if layer.startswith('RequestedLayerState{'):layer=layer.split('{',1)[1].split(' parentId=',1)[0]
def presents():return adb('shell',shlex.join(['dumpsys','SurfaceFlinger','--latency',layer]))
def ged():return json.loads(adb('exec-out','run-as',pkg,'files/runtime/mali-ged-query'))
apk=adb('shell','pm','path',pkg).strip().removeprefix('package:');assert apk.startswith('/') and '\n' not in apk
apk_sha=adb('shell',shlex.join(['sha256sum',apk])).split()[0]
sources=ROOT/'scripts/dev/probes/mali_frame_counters'
build=ROOT/'.local/build/mali-frame-counters-sdk'
binary=build/'mali_frame_counter_probe'
stamp=build/'source-inputs.json'
inputs=[sources/'main.cpp',sources/'CMakeLists.txt',args.arm_studio/'streamline/gator/daemon/mali_userspace/MaliHwCntrNamesGenerated.h']
inputs.extend(p for p in (args.arm_studio/'streamline/gator/daemon/hwcpipe2/device').rglob('*')
              if p.is_file() and (p.suffix in ('.cpp','.hpp','.h') or p.name=='CMakeLists.txt'))
source_hashes={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
if args.deploy:
 build.mkdir(parents=True,exist_ok=True)
 ndk=Path(os.environ.get('ANDROID_NDK_HOME',str(ROOT/'.local/android-sdk/ndk/28.2.13676358')))
 with (build/'build.log').open('w') as log:
  subprocess.run(['cmake','-S',str(sources),'-B',str(build),'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=29','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release','-DHWCPIPE_SOURCE='+str(args.arm_studio.resolve()/'streamline/gator/daemon/hwcpipe2'),'-DGATOR_SOURCE='+str(args.arm_studio.resolve()/'streamline/gator/daemon')],stdout=log,stderr=subprocess.STDOUT,check=True)
  subprocess.run(['cmake','--build',str(build),'-j4'],stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run(base+['push',str(binary),'/data/local/tmp/d2r-mali-frame-counter-probe'],check=True,stdout=subprocess.DEVNULL)
 adb('shell','run-as',pkg,'cp','/data/local/tmp/d2r-mali-frame-counter-probe','files/runtime/mali-frame-counter-probe')
 adb('shell','run-as',pkg,'chmod','700','files/runtime/mali-frame-counter-probe')
 stamp.write_text(json.dumps(source_hashes,indent=2)+'\n')
assert binary.exists() and stamp.exists() and json.loads(stamp.read_text())==source_hashes,'deploy the current SDK helper first'
binary_sha=hashlib.sha256(binary.read_bytes()).hexdigest()
assert adb('exec-out','run-as',pkg,'sha256sum','files/runtime/mali-frame-counter-probe').split()[0]==binary_sha
chain=adb('exec-out','run-as',pkg,'cat','files/runtime/shaders/.chain').strip()
before=state();assert before['world_input_ready']
thermal0=adb('shell','dumpsys','thermalservice');ged0=ged();paths['before'].write_text(presents())
with paths['raw'].open('w') as out,paths['err'].open('w') as err:
 subprocess.run(base+['exec-out','run-as',pkg,'files/runtime/mali-frame-counter-probe'],stdout=out,stderr=err,timeout=15,check=True)
raw_rows=[json.loads(s) for s in paths['raw'].read_text().splitlines()]
assert len(raw_rows)==41 and raw_rows[-1].get('end_ns')
raw_hits=sum(r['raw_before_ns']<=r['end_ns']<=r['raw_after_ns'] for r in raw_rows[1:])
mono_hits=sum(r['mono_before_ns']<=r['end_ns']<=r['mono_after_ns'] for r in raw_rows[1:])
if raw_hits==40:sample_clock='monotonic_raw'
elif mono_hits==40:sample_clock='monotonic'
else:raise SystemExit('counter timestamps do not fit either observed clock bracket')
# Let a completed present beyond the counter endpoint enter SurfaceFlinger's
# history. Polling is after the measurement, never per-frame during sampling.
deadline=time.monotonic()+5
while True:
 history=presents();times=[]
 for line in history.splitlines():
  words=line.split()
  if len(words)==3 and all(x.isdecimal() for x in words) and 0<int(words[1])<2**63-1:times.append(int(words[1]))
 last=raw_rows[-1];offset=(last['mono_before_ns']+last['mono_after_ns']-last['raw_before_ns']-last['raw_after_ns'])//2 if sample_clock=='monotonic_raw' else 0
 if times and max(times)>last['end_ns']+offset+1000000:break
 if time.monotonic()>deadline:raise SystemExit('no completed present beyond counter window; no normalization accepted')
 time.sleep(.1)
paths['after'].write_text(history);ged1=ged();after=state()
assert adb('shell','pidof',pkg).strip()==pid
assert adb('exec-out','run-as',pkg,'cat','files/runtime/shaders/.chain').strip()==chain
for key in ['area','x','y','hp','scale_3d','game_output','surface_output']:assert before[key]==after[key],key
assert before['pipeline_prepare']['pipelines']==after['pipeline_prepare']['pipelines']
rows=[json.loads(s) for s in paths['raw'].read_text().splitlines()]
samples=[r for r in rows if 'sample' in r];assert len(samples)==40 and rows[0].get('metadata')
assert all(r['sample']==i and r['user_data']==i and not r['error'] and r['stretched'] in (False,None) for i,r in enumerate(samples))
assert ged0['dvfs_query']['effective_ceiling_khz']==ged1['dvfs_query']['effective_ceiling_khz']
report={'pid':int(pid),'apk_sha256':apk_sha,'shader_chain':chain,'helper_sha256':binary_sha,
        'sample_clock':sample_clock,'clock_observation':{'monotonic_raw_bracket_hits':raw_hits,'monotonic_bracket_hits':mono_hits},
        'source_inputs':source_hashes,'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'before':before,'after':after,'ged_before':ged0,'ged_after':ged1,
        'thermal_before':thermal0,'thermal_after':adb('shell','dumpsys','thermalservice'),
        'raw_sha256':hashlib.sha256(paths['raw'].read_bytes()).hexdigest(),'layer':layer,
        'scope':'Unmodified Arm SDK manual primary hardware counters, system GPU scope. No CPU stacks, GPU layer, or per-draw timing. Raw sample clock must be checked against recorded monotonic/raw bounds before normalization to actual presents. Not exclusive game counters.'}
paths['meta'].write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'pid':int(pid),'samples':len(samples),'ceiling_khz':ged0['dvfs_query']['effective_ceiling_khz']}))
