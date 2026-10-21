#!/usr/bin/env python3
"""通过原 Android 生命周期保存当前游戏；失败时返回非零，禁止继续安装/重启。
Save through the original Android lifecycle; fail closed before install/restart.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

PACKAGE='com.d2rmobile.nativeport'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--resume',action='store_true',help='resume the same process after confirmed saving')
    parser.add_argument('--vma-dump-on-suspend',action='store_true',help='request one builtin VMA dump in this original pause; default off')
    parser.add_argument('--timeout',type=float,default=20)
    args=parser.parse_args()
    if args.report.exists() or not 1<=args.timeout<=60:
        parser.error('use a new report and timeout from 1 to 60 seconds')

    def adb(*parts):
        return subprocess.check_output(['adb','-s',args.serial,*parts],text=True,timeout=20)

    pid=adb('shell','pidof',PACKAGE).strip()
    if not pid.isdecimal():raise SystemExit('one running original game process is required')
    log=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/native.log')
    if f'Android native start pid={pid}\n' not in log:
        raise SystemExit('current PID does not match the original game log')

    # HOME alone generates no new pause when already backgrounded. Foreground
    # first, then establish a fresh log boundary and request the original pause.
    if args.vma_dump_on_suspend:
        # Carry the request in this helper's single foreground Intent. Wait
        # for this PID's actual onNewIntent log before the original fresh-save
        # boundary/HOME; am start returning is not callback acknowledgement.
        request_prefix=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/native.log')
        adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity',
            '--ez','vma_dump_on_suspend','true')
        request_deadline=time.monotonic()+args.timeout
        request_confirmation=None
        while time.monotonic()<request_deadline:
            if adb('shell','pidof',PACKAGE).strip()!=pid:
                raise SystemExit('PID changed while requesting VMA dump; no HOME or success accepted')
            current=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/native.log')
            if not current.startswith(request_prefix):
                raise SystemExit('game log reset while requesting VMA dump')
            requests=list(re.finditer(r'^.*\[vulkan VMA dump\] explicit request accepted=([01]); consumes at next normal suspend[^\n]*',current[len(request_prefix):],re.M))
            if len(requests)>1:
                raise SystemExit('multiple fresh VMA request confirmations; no HOME accepted')
            if requests:
                if requests[0].group(1)!='1':
                    raise SystemExit('fresh VMA request rejected; no HOME accepted')
                request_confirmation=requests[0].group(0)
                break
            time.sleep(.2)
        if not request_confirmation:
            raise SystemExit('no fresh accepted VMA request confirmation; no HOME accepted')
    else:
        adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity')
    offset=len(adb('exec-out','run-as',PACKAGE,'cat','files/runtime/native.log'))
    adb('shell','input','keyevent','HOME')
    deadline=time.monotonic()+args.timeout
    match=None
    while time.monotonic()<deadline:
        if adb('shell','pidof',PACKAGE).strip()!=pid:
            raise SystemExit('PID changed while saving; no success accepted')
        current=adb('exec-out','run-as',PACKAGE,'cat','files/runtime/native.log')
        if len(current)<offset:raise SystemExit('game log reset while saving')
        match=re.search(r'^.*Android background save asked=1 commits=2[^\n]*',current[offset:],re.M)
        if match:break
        time.sleep(.2)
    if not match:raise SystemExit('no fresh original save confirmation; leave suspended and stop dependent work')
    result=dict(pid=int(pid),serial=args.serial,fresh_original_lifecycle_save=True,
                save_commits=2,confirmation=match.group(0),resumed_same_process=False)
    if args.vma_dump_on_suspend:
        result.update(vma_dump_requested=True,vma_request_confirmation=request_confirmation)
    if args.resume:
        adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity')
        if adb('shell','pidof',PACKAGE).strip()!=pid:raise SystemExit('PID changed on resume')
        result['resumed_same_process']=True
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':
    main()
