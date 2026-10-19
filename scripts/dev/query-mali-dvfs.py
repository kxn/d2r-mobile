#!/usr/bin/env python3
"""Deploy/read the independent, read-only MT6983 GED profiling probe.

Does not link GPL driver ABI declarations into the game. No boost, target-PID,
frequency, hint or thermal setters are issued. Package UID permissions apply.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
PACKAGE='com.d2rmobile.nativeport'
REMOTE='files/runtime/mali-ged-query'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--deploy',action='store_true')
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():parser.error('use a new report')
    base=['adb','-s',args.serial]
    def adb(*parts):return subprocess.check_output([*base,*parts],text=True,timeout=20)
    platform=adb('shell','getprop','ro.board.platform').strip()
    if platform!='mt6983':raise SystemExit('this audited vendor ABI probe is restricted to the tested MT6983 platform')
    source=ROOT/'scripts/dev/probes/mali-ged-query.c'
    output=ROOT/'.local/build/mali-ged-query';output.mkdir(parents=True,exist_ok=True)
    binary=output/'mali-ged-query'
    if args.deploy:
        ndk=Path(os.environ.get('ANDROID_NDK_HOME',str(ROOT/'.local/android-sdk/ndk/28.2.13676358')))
        compiler=ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang'
        subprocess.run([str(compiler),'-O2',str(source),'-o',str(binary)],check=True)
        subprocess.run([*base,'push',str(binary),'/data/local/tmp/d2r-mali-ged-query'],check=True,
                       stdout=subprocess.DEVNULL)
        adb('shell','run-as',PACKAGE,'cp','/data/local/tmp/d2r-mali-ged-query',REMOTE)
        adb('shell','run-as',PACKAGE,'chmod','700',REMOTE)
        (output/'source.sha256').write_text(hashlib.sha256(source.read_bytes()).hexdigest()+'\n')
    if not binary.exists() or not (output/'source.sha256').exists():raise SystemExit('deploy this probe first')
    if (output/'source.sha256').read_text().strip()!=hashlib.sha256(source.read_bytes()).hexdigest():
        raise SystemExit('source changed; redeploy before reading')
    digest=hashlib.sha256(binary.read_bytes()).hexdigest()
    if adb('exec-out','run-as',PACKAGE,'sha256sum',REMOTE).split()[0]!=digest:
        raise SystemExit('deployed helper does not match local binary')
    values=json.loads(adb('exec-out','run-as',PACKAGE,REMOTE))
    dvfs=values.get('dvfs_query',{})
    if dvfs.get('error')!=0 or not 0<dvfs.get('effective_ceiling_khz',0)<=values.get('hardware_max_frequency_khz',0)<2000000:
        raise SystemExit('vendor DVFS query is unavailable or invalid; no ceiling inferred')
    report={'serial':args.serial,'platform':platform,'helper_sha256':digest,
            'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'values':values,
            'scope':'Boundary snapshot of system GPU counters, not exclusive game utilization. '
                    'QUERY_GPU_DVFS_INFO controls remain zero; no target PID or GIFT hint is changed. '
                    'Effective ceiling reflects the most recent GPU-done update; no limiting-policy ID is exposed.',
            'abi_reference':'https://github.com/OnePlusOSS/android_kernel_5.10_oneplus_mt6983/tree/'
                            'oneplus/mt6983_t_13.0_oneplus_ace_2v/drivers/gpu/mediatek/ged'}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(values))


if __name__=='__main__':main()
