"""Bounded official LLDB startup breakpoints; no inferior function evaluation.
The companion SDK decoder is an exact byte copy of accepted 44c acquisition code.
The controller owns original launch/save/stop/restoration, not this hook.
"""
import json
import os
from pathlib import Path
import subprocess
import time
import sdk_free_resident as sdk

R = Path('/home/dl/d2r-mobile')
ENTRY = 0x2f9974
EPILOGUE = 0x2f9a54
ENTRY_SIZE = 0xf4


def require(ok, why):
    if not ok:
        raise ValueError(why)


def qualifying_return(values, allocator, minimum):
    require(set(values)=={'x0','x19','x20','x21','x29','sp','pc'}, 'missing return registers')
    require(values['x21']==allocator and values['x20']>=minimum, 'wrong allocator/size stop')
    require(values['x0']==0 or values['x0']+values['x20']<1<<64, 'returned extent overflows')
    return dict(pointer=values['x0'],requested_bytes=values['x20'],alignment=values['x19'],
                allocator=allocator,null_result=values['x0']==0)


def breakpoint_thread(process, bp):
    import lldb
    threads=[]
    for t in process:
        reason=t.GetStopReason()
        if reason in (lldb.eStopReasonNone,lldb.eStopReasonInvalid):
            continue
        require(reason==lldb.eStopReasonBreakpoint, 'unrelated non-breakpoint stop')
        data=[t.GetStopReasonDataAtIndex(i) for i in range(t.GetStopReasonDataCount())]
        require(len(data)==2 and data[0]==bp.GetID(), 'unrelated/ambiguous breakpoint stop')
        threads.append(t)
    require(len(threads)==1, 'not one exact owned breakpoint thread')
    return threads[0]


def wait_stop(process, last_stop, deadline):
    import lldb
    while time.monotonic()<deadline:
        state=process.GetState()
        require(state not in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateCrashed,
                              lldb.eStateInvalid,lldb.eStateUnloaded), 'owned process exited/detached/crashed')
        if state==lldb.eStateStopped and process.GetStopID()>last_stop:
            return True
        time.sleep(.02)  # Existing debugger event-control deadline; never a profiler.
    return False


def output_command(debugger, command):
    import lldb
    out=lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand(command,out)
    require(out.Succeeded(), 'official LLDB command failed: '+command+': '+(out.GetError() or ''))
    return out.GetOutput() or ''


def snapshot(debugger):
    import lldb
    config=json.loads(Path(os.environ['DM_SDK_FREE_CONFIG']).read_text())
    output=Path(os.environ['DM_SDK_FREE_OUTPUT']); pid=int(os.environ['DM_SDK_FREE_PID'])
    target=debugger.GetSelectedTarget(); process=target.GetProcess()
    result=dict(PID=pid,ELF_sha256=config['ELF_sha256'],complete=False,inferior_calls=False,
                target_function_evaluation=False,profile=False,errors=[],returns=[],
                scope='First bounded large original SDK returns only, not complete malloc history or ownership/reclaim permission')
    bp=None; allocbp=None
    was_async=debugger.GetAsync()
    try:
        require(process.IsValid() and process.GetState()==lldb.eStateStopped and process.GetProcessID()==pid,
                'no matching initial owned live stop')
        require(target.GetTriple().startswith('aarch64'), 'not native AArch64')
        # Remote attach must identify the real process executable first. Formal
        # host symbols remain unloaded until Android's actual dlopen event.
        require(target.GetExecutable().IsValid() and target.GetExecutable().GetFilename()!='libdmhost_android.so',
                'formal host SO was incorrectly selected as process executable')
        formal=target.FindModule(lldb.SBFileSpec(config['ELF']))
        require(formal.IsValid() and formal.GetUUIDString().replace('-','').lower()==config['ELF_BuildID']
                and formal.GetObjectFileHeaderAddress().GetLoadAddress(target)==lldb.LLDB_INVALID_ADDRESS,
                'formal host symbols already have a load address before native startup')
        bp=target.BreakpointCreateByName('guest_thread_init','libdmhost_android.so')
        require(bp.IsValid(), 'could not create pending host startup breakpoint')
        result['pending_host_breakpoint']=dict(id=bp.GetID(),name='guest_thread_init',locations_before_load=bp.GetNumLocations())
        debugger.SetAsync(True)
        last=process.GetStopID(); err=process.Continue();require(err.Success(), 'initial Continue failed: '+str(err))
        adb=[os.environ['DM_SDK_FREE_ADB'],'-s',config['serial']]
        # Outer owned wrapper starts the original NDK JDWP release only after
        # this live attached process has the pending breakpoint and is continued.
        (output/'pending-startup-armed.json').write_text(json.dumps(dict(PID=pid,ELF_sha256=config['ELF_sha256'],
            pending_host_breakpoint='guest_thread_init',initial_stop_id=last,inferior_calls=False))+'\n')
        deadline=time.monotonic()+config['startup_allocation']['natural_wait_seconds']
        require(wait_stop(process,last,deadline), 'pending startup breakpoint not hit within bound')
        thread=breakpoint_thread(process,bp); frame=thread.GetFrameAtIndex(0)
        require('guest_thread_init' in (frame.GetFunctionName() or ''), 'wrong pending host function')
        module=frame.GetModule(); uuid=module.GetUUIDString().replace('-','').lower()
        require(uuid==config['ELF_BuildID'], 'actual loaded host UUID differs')
        callers=[thread.GetFrameAtIndex(i).GetFunctionName() or '' for i in range(min(thread.GetNumFrames(),16))]
        require(any('dm_android_main' in n for n in callers), 'guest_thread_init is not the original main startup path')
        maps=subprocess.check_output(adb+['exec-out','run-as','com.d2rmobile.nativeport','cat','/proc/'+str(pid)+'/maps'],text=True,timeout=15)
        (output/'native-debugger-maps-loaded.txt').write_text(maps)
        rows=sdk.parse_maps(maps)
        hostpaths={r['name'] for r in rows if r['name'].endswith('/libdmhost_android.so')}
        require(len(hostpaths)==1, 'host mapping path absent/ambiguous')
        hostpath=next(iter(hostpaths)); actual=subprocess.check_output(adb+['exec-out','run-as','com.d2rmobile.nativeport','sha256sum',hostpath],text=True,timeout=15).split()[0]
        require(actual==config['ELF_sha256'], 'actual loaded host SHA differs')
        mods,declarations=sdk._read_modules(target)
        main=next(m for m in mods if m['name']=='main'); sm=next(m for m in mods if m['name']=='sdk')
        require(sm['base']>0 and sm['bytes']>ENTRY+ENTRY_SIZE and main['bytes']>sdk.ALLOC_OFFSET+8, 'invalid actual guest modules')
        flat=Path(config['sdk_flat']['path']).read_bytes()
        actualcode=sdk._read_small(process,sm['base']+ENTRY,ENTRY_SIZE)
        require(actualcode==flat[ENTRY:ENTRY+ENTRY_SIZE], 'live original SDK Allocate bytes differ from reviewed unchanged function')
        (output/'original-SDK-Allocate-live.bin').write_bytes(actualcode)
        result.update(host_UUID=uuid,host_mapped_path=hostpath,modules=mods,module_declarations=declarations,
                      startup_stop_id=process.GetStopID(),startup_thread=thread.GetThreadID(),startup_callers=callers,
                      original_SDK_code_verified=True,allocator=main['base']+sdk.ALLOC_OFFSET)
        target.BreakpointDelete(bp.GetID());bp=None
        allocbp=target.BreakpointCreateByAddress(sm['base']+EPILOGUE)
        require(allocbp.IsValid() and allocbp.GetNumLocations()==1, 'SDK return breakpoint unresolved')
        allocator=result['allocator']; minimum=config['startup_allocation']['minimum_bytes']
        allocbp.SetCondition('$x21 == '+hex(allocator)+' && $x20 >= '+str(minimum))
        result['return_condition']=allocbp.GetCondition()
        last=process.GetStopID();err=process.Continue();require(err.Success(),'Continue to allocator failed: '+str(err))
        limit=config['startup_allocation']['max_returns'];used=0
        while len(result['returns'])<limit and wait_stop(process,last,deadline):
            thread=breakpoint_thread(process,allocbp);frame=thread.GetFrameAtIndex(0)
            values={name:sdk._unsigned(frame.FindRegister(name)) for name in ('x0','x19','x20','x21','x29','sp','pc')}
            require(values['pc']==sm['base']+EPILOGUE, 'return PC differs')
            row=qualifying_return(values,allocator,minimum)
            row.update(PID=pid,TID=thread.GetThreadID(),stop_id=process.GetStopID(),registers=values,
                       saved_caller_LR=int.from_bytes(sdk._read_small(process,values['x29']+8,8),'little'))
            process.SetSelectedThread(thread)
            row['official_backtrace']=output_command(debugger,'thread backtrace -c 64')
            used+=len(json.dumps(row).encode());require(used<=config['startup_allocation']['metadata_budget_bytes'],'stack metadata budget exceeded')
            result['returns'].append(row)
            # Persist each accepted event; a subsequent exit never erases provenance.
            (output/'startup-allocations-progress.json').write_text(json.dumps(result,indent=2)+'\n')
            last=process.GetStopID();err=process.Continue();require(err.Success(),'Continue after return failed: '+str(err))
        result['accepted_hit_limit_reached']=len(result['returns'])==limit
        result['natural_window_expired']=time.monotonic()>=deadline
        result['allocation_history_truncated']=True
        require(result['returns'], 'no large allocator return observed; bounded window is inconclusive')
        # No invented stack across the guest/host bridge. Saved LR may be host.
        result['caller_limitation']='Retain official unwind as-is. Missing guest ancestry is UNKNOWN; do not synthesize a caller.'
        result['complete']=True
    except BaseException as e:
        result['errors'].append(repr(e))
    finally:
        try:
            if process.IsValid() and process.GetState() not in (lldb.eStateDetached,lldb.eStateExited):
                if process.GetState()!=lldb.eStateStopped:
                    process.SendAsyncInterrupt()
                    require(wait_stop(process,-1,time.monotonic()+5), 'cleanup interrupt did not stop')
                for b in (bp,allocbp):
                    if b is not None and b.IsValid():
                        require(target.BreakpointDelete(b.GetID()), 'owned breakpoint removal failed')
                error=process.Detach();require(error.Success(),'Detach failed: '+str(error));result['detached']=True
            else: result['detached']=process.GetState()==lldb.eStateDetached
        except BaseException as e:result['errors'].append('owned detach: '+repr(e))
        debugger.SetAsync(was_async)
        result['complete']=result['complete'] and result.get('detached') is True and not result['errors']
        result['status']='SDK_STARTUP_LARGE_RETURNS_CAPTURE_COMPLETE' if result['complete'] else 'SDK_STARTUP_LARGE_RETURNS_INCONCLUSIVE'
        (output/'startup-allocations.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({'startup_allocation_status':result['status'],'accepted_returns':len(result['returns']),'errors':result['errors']}))
