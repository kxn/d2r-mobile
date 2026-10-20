#!/usr/bin/env python3
"""用官方 gatord 附加到当前真实游戏进程，导出 Mali-G710 计数器记录。
Attach official gatord to the running native game and export Mali-G710 counters.
"""
import argparse
import hashlib
import json
import shlex
from pathlib import Path
import signal
import subprocess
import tarfile
import uuid
import time

ROOT = Path('/home/dl/d2r-mobile')  # same existing official tool suite after wrapper relocation
SUITE = ROOT / '.local/tools/Arm_Performance_Studio_2026.5'
PACKAGE = 'com.d2rmobile.nativeport'
COUNTERS = ['GPU_ACTIVE', 'ITER_TILER_ACTIVE', 'ITER_FRAG_ACTIVE', 'FRAG_ACTIVE',
            'COMPUTE_ACTIVE', 'EXEC_CORE_ACTIVE', 'L2_EXT_READ_BEATS',
            'L2_EXT_WRITE_BEATS', 'FRAG_QUADS_RAST', 'TEX_TFCH_CLK_STALLED']
TRIAGE = ['GPU_ITER_ACTIVE','FRAG_PTILES','FRAG_TRANS_ELIM','EXEC_INSTR_FMA','EXEC_INSTR_CVT',
          'EXEC_INSTR_SFU','EXEC_INSTR_DIVERGED','EXEC_ICACHE_MISS','EXEC_STARVE_ARITH',
          'LS_MEM_READ_FULL','LS_MEM_READ_SHORT','LS_MEM_WRITE_FULL','LS_MEM_WRITE_SHORT',
          'BEATS_RD_LSC','BEATS_RD_LSC_EXT','BEATS_RD_TEX','BEATS_RD_TEX_EXT',
          'BEATS_WR_LSC_OTHER','BEATS_WR_LSC_WB','BEATS_WR_TIB',
          'L2_EXT_AR_STALL','L2_EXT_W_STALL','TEX_FILT_NUM_OPERATIONS',
          'TEX_DFCH_CLK_STALLED','TEX_TFCH_STARVED_PENDING_DATA_FETCH']
OCCLUSION = ['FRAG_FPK_ACTIVE','FRAG_WARPS','FRAG_QUADS_EZS_TEST',
             'FRAG_QUADS_EZS_UPDATE','FRAG_QUADS_EZS_KILL','FRAG_LZS_TEST',
             'FRAG_LZS_KILL','QUAD_FPK_KILLER']
SCHEDULING = ['MCU_ACTIVE', 'ITER_COMP_ACTIVE', 'ITER_TILER_READY_BLOCKED',
              'ITER_FRAG_READY_BLOCKED', 'ITER_COMP_READY_BLOCKED',
              'GPU_IRQ_ACTIVE', 'CACHE_FLUSH_CYCLES']
# Inputs required by Arm's stock Mali-G710 Performance Advisor report. This
# only selects official gatord counters; all normalization/analysis stays in PA.
ADVISOR = ['LS_MEM_ATOMIC', 'VARY_SLOT_16', 'VARY_SLOT_32', 'PRIM_VISIBLE',
           'PRIM_CULLED', 'PRIM_CLIPPED', 'PRIM_SAT_CULLED',
           'ITER_FRAG_TASK_COMPLETED', 'FRAG_WARPS']
# Official CSF scheduling-dependency and endpoint-drain counters. GPU_ACTIVE
# includes queued work, so it cannot by itself exclude synchronization stalls.
DEPENDENCIES = ['CACHE_FLUSH', 'CEU_ACTIVE', 'LSU_ACTIVE',
                'ITER_TILER_EP_DRAIN', 'ITER_COMP_EP_DRAIN'] + [
                    'CSHWIF%d_%s' % (stream, event)
                    for stream in range(4) for event in ('ENABLED','WAIT_BLOCKED')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
    parser.add_argument('--seconds', type=int, default=15)
    parser.add_argument('--preset', choices=['basic','triage','occlusion','scheduling','advisor','dependencies'], default='basic')
    parser.add_argument('--official-replay', action='store_true', help='attach to the official Arm frame replay instead of the native game; reports are replay performance, not game FPS')
    parser.add_argument('--call-stacks', action='store_true', help='additional CPU sampling overhead')
    parser.add_argument('--output', type=Path, required=True, help='new .apc directory')
    args = parser.parse_args()
    package = 'com.arm.frameadvisor.replay' if args.official_replay else PACKAGE
    output = args.output.resolve()
    if not 1 <= args.seconds <= 60:
        parser.error('--seconds must be 1..60')
    if output.suffix != '.apc' or output.exists():
        parser.error('--output must be a new .apc directory')
    output.parent.mkdir(parents=True, exist_ok=True)
    daemon = SUITE / 'streamline/bin/android/arm64/gatord'
    if not daemon.is_file():
        raise SystemExit('run install-arm-performance-studio.py first')
    base = ['adb', '-s', args.serial]

    def adb(*parts):
        return subprocess.check_output([*base, *parts], text=True, timeout=20)

    pid = adb('shell', 'pidof', package).strip()
    if not pid.isdecimal():
        raise SystemExit('one running native game process is required')
    if not args.official_replay:
        log = adb('exec-out', 'run-as', package, 'cat', 'files/runtime/native.log')
        if (f'Android native start pid={pid}\n' not in log or
                'external profiling: custom CPU/GPU timers disabled' not in log):
            raise SystemExit('start the current game with --external-profile before capture')
    vk = json.loads(adb('shell', 'cmd', 'gpu', 'vkjson'))
    if not any('Mali-G710' in d['properties']['deviceName'] for d in vk['devices']):
        raise SystemExit('this counter preset is verified for Mali-G710 only')
    token = 'd2r-' + uuid.uuid4().hex
    temporary = '/data/local/tmp/' + token
    leaf = token + '.apc'
    incoming = 'files/runtime/profiles/' + leaf
    adb('push', str(daemon), temporary)
    adb('shell', 'run-as', package, 'cp', temporary, './d2r-gatord')
    adb('shell', 'rm', temporary)
    adb('shell', 'run-as', package, 'chmod', '700', './d2r-gatord')
    adb('shell', 'run-as', package, 'mkdir', '-p', 'files/runtime/profiles')
    old = adb('shell', 'getprop', 'security.perf_harden').strip()
    if old not in ('0', '1'):
        raise SystemExit('unrecognized profiling property; left unchanged')
    # 官方 setup 同样临时开启 perf 权限；退出或中断后恢复原值。
    # Match official setup's temporary perf access; restore even on interruption.
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    result = None
    counters = ['ARM_Mali-G710_' + c for c in COUNTERS +
                (TRIAGE if args.preset!='basic' else []) +
                (OCCLUSION if args.preset=='occlusion' else []) +
                (SCHEDULING if args.preset in ('scheduling','advisor','dependencies') else []) +
                (ADVISOR if args.preset in ('advisor','dependencies') else []) +
                (DEPENDENCIES if args.preset=='dependencies' else [])]
    # Device enumeration confirms that this G710 driver has no frequency
    # counter. Do not request it for the complete Advisor configuration.
    if args.preset not in ('basic','advisor','dependencies'):
        counters.append('ARM_Mali-clock-0')
    apk=adb('shell','pm','path',package).strip().removeprefix('package:')
    if '\n' in apk or not apk.startswith('/'):
        raise SystemExit('one native APK path is required for capture provenance')
    apk_sha=adb('shell',shlex.join(['sha256sum',apk])).split()[0]
    shader_chain=None if args.official_replay else adb('exec-out','run-as',package,'cat','files/runtime/shaders/.chain').strip()
    def snapshot():
        state=None
        if not args.official_replay:
            raw=adb('exec-out','run-as',package,'tail','-n','1','files/runtime/game-state.jsonl')
            state=json.loads(raw)
        return {'host_monotonic_s':time.monotonic(),'state':state,
                'thermal':'Not queried: app public API DmThermal log only'}
    before=snapshot()
    def stop_owned_daemon():
        # A host adb timeout does not terminate its remote gatord children.
        # Match this unique output token and the app UID, then its children;
        # never stop another capture or an unrelated system service.
        uid=adb('shell','run-as',package,'id','-un').strip()
        rows=[line.split() for line in adb('shell','ps','-A','-o','PID,PPID,USER,NAME').splitlines()[1:]]
        processes={int(r[0]):int(r[1]) for r in rows
                   if len(r)==4 and r[2]==uid and r[3]=='d2r-gatord'}
        owned=set()
        for candidate in processes:
            try:
                command=adb('exec-out','run-as',package,'cat',f'/proc/{candidate}/cmdline')
                if incoming in command.split('\0'):owned.add(candidate)
            except subprocess.CalledProcessError:
                pass  # The official tool may have just finished normally.
        while True:
            children={p for p,parent in processes.items() if parent in owned}
            if children<=owned:break
            owned|=children
        for candidate in sorted(owned):
            subprocess.run([*base,'shell','run-as',package,'kill','-TERM',str(candidate)],
                           stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=10)
        if owned:
            output.with_suffix('.capture-cleanup.json').write_text(json.dumps(
                {'scope':'Only this capture output token and app UID',
                 'terminated_daemon_pids':sorted(owned)},indent=2)+'\n')
    try:
        adb('shell', 'setprop', 'security.perf_harden', '0')
        if adb('shell', 'getprop', 'security.perf_harden').strip() != '0':
            raise SystemExit('profiling permission could not be enabled')
        with output.with_suffix('.capture.log').open('w') as capture_log:
            result = subprocess.run([*base, 'shell', 'run-as', package, './d2r-gatord',
                '--pid', pid, '--system-wide', 'no', '--exclude-kernel', 'yes',
                '--call-stack-unwinding', 'yes' if args.call_stacks else 'no', '--sample-rate', 'low',
                '--max-duration', str(args.seconds), '--gpu-timeline', 'no',
                '--output', incoming, '--counters', ','.join(counters)],
                stdout=capture_log, stderr=subprocess.STDOUT, timeout=args.seconds + 30)
    finally:
        try:
            stop_owned_daemon()
        finally:
            adb('shell', 'setprop', 'security.perf_harden', old)
    after=snapshot()
    if result is None or result.returncode:
        raise SystemExit('gatord capture failed; see capture log')
    if adb('shell', 'pidof', package).strip() != pid:
        raise SystemExit('target process changed during capture; not accepted')
    if not args.official_replay and adb('exec-out','run-as',package,'cat','files/runtime/shaders/.chain').strip()!=shader_chain:
        raise SystemExit('shader cache changed during capture; not accepted')
    archive = output.with_suffix('.apc.tar')
    with archive.open('wb') as stream:
        subprocess.run([*base, 'exec-out', 'run-as', package, 'tar', '-cf', '-',
                        '-C', 'files/runtime/profiles', leaf], stdout=stream, check=True)
    with tarfile.open(archive) as bundle:
        if any(Path(m.name).parts[0] != leaf for m in bundle):
            raise SystemExit('unexpected capture archive root')
        bundle.extractall(output.parent, filter='data')
    (output.parent / leaf).rename(output)
    if not (output / 'captured.xml').is_file() or not (output / '0000000000').stat().st_size:
        raise SystemExit('empty/incomplete Streamline capture')
    # 只删除本次工具生成的远端记录，不碰游戏存档或资源。
    # Remove only this tool-generated remote capture, never game saves/assets.
    adb('shell', 'run-as', package, 'rm', '-r', incoming)
    metadata = {'serial': args.serial, 'pid': int(pid), 'seconds_requested': args.seconds,
                'tool': 'Arm gatord 9.9 / Performance Studio 2026.5',
                'scope': 'system GPU counters; CPU perf attached to selected PID',
                'package':package, 'official_replay':args.official_replay,
                'game_fps_measurement':not args.official_replay,
                'custom_timers': False, 'call_stacks': args.call_stacks, 'gpu_timeline_layer': False,
                'sample_rate': 'low; see captured.xml for actual rates',
                'perf_harden_restored': adb('shell', 'getprop', 'security.perf_harden').strip() == old,
                'daemon_sha256': hashlib.sha256(daemon.read_bytes()).hexdigest(),
                'preset':args.preset, 'counters': counters, 'capture': str(output)}
    metadata.update(apk_sha256=apk_sha,shader_chain=shader_chain,
                    capture_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    game_state_bracket={'before':before,'after':after,
                        'limitation':'Bracket includes daemon setup/teardown; not exact counter-frame normalization or GPU wall time.'})
    output.with_suffix('.capture.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps(metadata))


if __name__ == '__main__':
    main()
