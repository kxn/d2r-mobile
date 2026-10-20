"""Optional existing ADPF Intent + existing official Perfetto, same owned PID."""
from pathlib import Path
import json
import os
import subprocess
import sys
import time

R=Path('/home/dl/d2r-mobile')


class Inapplicable(RuntimeError):pass


def run(dev,pid,p,out,phase_report,d,m):
    root=out/'optional-adpf';root.mkdir()
    result={'enabled':True,'outcome':'pending','experiment_complete':False,
            'scope':'ExistingADPF policy experiment, not defaultenable or heat/power improvement',
            'cleanup_errors':[]}
    baseline=out/'official-performance/completion-scheduler.json'
    b=m.read(baseline)
    m.require(b['pid']==int(pid) and b['apk_sha256']==p['new_apk'] and
              b['trace_sha256']==m.sha(baseline.with_suffix('.pftrace')),
              'ADPF first OFF must be the actual completed baseline')
    result['first_OFF']={'path':str(baseline),'trace_sha256':b['trace_sha256']}
    base=b['after'];base_hint=base['performance_hint']
    m.require(b['before']['performance_hint']['reference'] is True and
              b['before']['performance_hint']['active']==0 and
              base_hint['reference'] is True and base_hint['active']==0,
              'original officialbaseline was not OFF')
    token_report=root/'adpf-result.json'
    save=lambda:m.journal(token_report,result)
    env=os.environ.copy();env['PATH']=str(R/'.local/android-sdk/platform-tools')+':'+env.get('PATH','')

    def owned():
        m.require(dev.adb('shell','pidof '+m.PKG+' || true')==pid,'ADPF owned PID changed')

    def state(label,strict=True):
        owned()
        value=json.loads(dev.app('tail','-n','1','files/runtime/game-state.jsonl'))
        if not strict:
            (root/(label+'.json')).write_text(json.dumps(value,indent=2)+'\n')
            return value
        for key in ('area','x','y','scale_3d','game_output','surface_output','frame_pacing'):
            if value.get(key)!=base.get(key):raise Inapplicable('original stationary scope changed: '+key)
        if not(value['valid'] and value['world_input_ready']):raise Inapplicable('original world notready')
        if value['pipeline_prepare']['pipelines']!=base['pipeline_prepare']['pipelines']:
            raise Inapplicable('PSO set changed')
        (root/(label+'.json')).write_text(json.dumps(value,indent=2)+'\n')
        return value

    def toggle(reference):
        owned()
        result.setdefault('intents',[]).append({'reference':reference});save()
        dev.adb('shell','am','start','-W','-n',m.PKG+'/.MainActivity','--ez',
                 'performance_hint_reference','true' if reference else 'false')
        owned()

    def ready(reference,cleanup=False):
        # Bounded existing-state readiness only, no new performance timer.
        for n in range(20):
            value=state(('cleanup-'if cleanup else'')+('OFF'if reference else'ON')+'-readiness-%02d'%n,strict=not cleanup)
            h=value['performance_hint']
            result.setdefault('readiness',[]).append(h);save()
            if reference and h['reference'] is True and h['active']==0:return value
            if not reference and h['reference'] is False:
                if h['available']==0 or h['failures']>base_hint['failures'] or h['last_error']:
                    raise Inapplicable('ADPF unavailable or session/report failure: '+str(h))
                if h['available']==1 and h['active']==1 and h['reports']>base_hint['reports']:
                    tid=h['tid']
                    if not(isinstance(tid,int) and tid>0 and
                            dev.app('cat','/proc/'+pid+'/task/'+str(tid)+'/comm')=='nvn-translate'):
                        raise Inapplicable('ADPF targetTID not actual NVNtranslation thread')
                    result['hint_owner_tid']=tid;save()
                    return value
            time.sleep(.2)
        raise Inapplicable('ADPF actual mode/session readiness not observed')

    def thermal(label):
        text=dev.adb('logcat','-d','--pid='+pid,'-v','threadtime','-s','DmThermal:I','AndroidRuntime:E')
        (root/(label+'-public-api-logcat.txt')).write_text(text)
        if 'original save and background suspend'in text:raise RuntimeError('publicAPI thermalpause; ADPF experiment interrupted')

    def require_mode(value,reference):
        h=value['performance_hint']
        if h['reference'] is not reference or h['active']!=(0 if reference else 1):
            raise Inapplicable('actualADPF modechanged during window')
        if not reference and (h['available']!=1 or h['failures']!=base_hint['failures'] or h['last_error']
                              or h['tid']!=result.get('hint_owner_tid')):
            raise Inapplicable('actualADPF unavailable/reportfailed during window')

    def capture(label,reference):
        before=state(label+'-before');require_mode(before,reference);thermal(label+'-before')
        m.screenshot_collect(dev,pid,d,phase_report,out,'ADPF-'+label+'-before')
        trace=root/(label+'.pftrace')
        cmd=[sys.executable,p['adpf_experiment']['scheduler_helper']['path'],'--serial',p['serial'],
             '--seconds','8','--buffer-mb','64','--app-markers','--clean-atrace-print',
             '--frame-timeline','--output',str(trace)]
        result.setdefault('windows',[]).append({'label':label,'command':cmd,'complete':False});save()
        with (root/(label+'-console.txt')).open('x')as stream:
            subprocess.run(cmd,env=env,check=True,stdout=stream,stderr=subprocess.STDOUT,timeout=65)
        actual=m.read(trace.with_suffix('.json'))
        m.require(actual['pid']==int(pid) and actual['apk_sha256']==p['new_apk'] and
                  actual['shader_chain']==p['baseline_data']['chain'] and
                  actual['trace_sha256']==m.sha(trace),'ADPF officialtrace identity mismatch')
        after=state(label+'-after');require_mode(after,reference);thermal(label+'-after')
        for boundary in ('before','after'):require_mode(actual[boundary],reference)
        if not reference and actual['after']['performance_hint']['reports']<=actual['before']['performance_hint']['reports']:
            raise Inapplicable('actualADPF reports didnot advance during hintwindow')
        m.require(after['time_ns']>before['time_ns'],'ADPF original state stopped advancing')
        result['windows'][-1].update(complete=True,trace_sha256=actual['trace_sha256']);save()

    save()
    try:
        toggle(False);ready(False);capture('hint-ON',False)
        toggle(True);ready(True);capture('reference-OFF-return',True)
        result['outcome']='actual-same-PID-OFF-ON-OFF-windows-complete-not-benefit-claim'
        result['experiment_complete']=True
    except Inapplicable as error:
        result['outcome']='actual-unsupported-failed-or-scope-changed-not-applicable'
        result['reason']=str(error)
    except BaseException as error:
        result['outcome']='actual-tool-or-game-failure'
        result['error']=repr(error);raise
    finally:
        try:
            toggle(True);ready(True,cleanup=True)
            result['reference_restored_active_zero']=True
        except BaseException as error:
            result['experiment_complete']=False
            result['cleanup_errors'].append(repr(error));save();raise
        save()
    return result
