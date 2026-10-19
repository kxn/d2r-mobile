#!/usr/bin/env python3
"""Save/background the original game, pause its process for cooling, then resume.

Only the identified app PID is signalled. GPU/thermal policies are never changed.
SIGCONT runs in finally; an optional Activity resume retains the same shader cache.
"""
import argparse
import json
from pathlib import Path
import signal
import subprocess
import sys
import time

PACKAGE='com.d2rmobile.nativeport'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--seconds',type=int,default=120)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--resume',action='store_true')
    args=parser.parse_args()
    save=args.report.with_suffix('.save.json')
    if not 10<=args.seconds<=300 or args.report.exists() or save.exists():
        parser.error('use a new report and duration from 10 to 300 seconds')
    base=['adb','-s',args.serial]

    def adb(*parts):
        return subprocess.check_output([*base,*parts],text=True,timeout=20)

    def thermal():return adb('shell','dumpsys','thermalservice')

    subprocess.run([sys.executable,str(Path(__file__).with_name('android-game-background-save.py')),
                    '--serial',args.serial,'--report',str(save)],check=True)
    saved=json.loads(save.read_text());pid=str(saved['pid'])
    if adb('shell','pidof',PACKAGE).strip()!=pid:raise SystemExit('game PID changed before cooling')
    result={'serial':args.serial,'pid':int(pid),'original_save':saved,'thermal_before':thermal(),
            'requested_seconds':args.seconds,'process_continued':False,'same_process_resumed':False,'observations':[]}
    stopped=False

    def interrupted(signum,frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,interrupted)
    started=time.monotonic()
    try:
        stopped=True
        adb('shell','run-as',PACKAGE,'kill','-STOP',pid)
        raw=adb('shell','run-as',PACKAGE,'cat','/proc/'+pid+'/stat')
        if raw[raw.rindex(')')+2:].split()[0] not in ('T','t'):
            raise RuntimeError('process did not enter debugger pause')
        while time.monotonic()-started<args.seconds:
            time.sleep(max(0,min(1,args.seconds-(time.monotonic()-started))))
            elapsed=time.monotonic()-started
            if len(result['observations'])*30<=elapsed:
                if adb('shell','pidof',PACKAGE).strip()!=pid:raise RuntimeError('paused game exited during cooling')
                result['observations'].append({'elapsed_s':elapsed,'thermal':thermal()})
    finally:
        if stopped:
            adb('shell','run-as',PACKAGE,'kill','-CONT',pid)
            result['process_continued']=True
        result['elapsed_s']=time.monotonic()-started
        result['thermal_after']=thermal()
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(result,indent=2)+'\n')
    if args.resume:
        adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity')
        if adb('shell','pidof',PACKAGE).strip()!=pid:raise SystemExit('PID changed while resuming')
        result['same_process_resumed']=True
        args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ['pid','elapsed_s','process_continued','same_process_resumed']}))


if __name__=='__main__':main()
