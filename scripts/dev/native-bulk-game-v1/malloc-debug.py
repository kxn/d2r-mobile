"""Official libc malloc_debug orchestration and official viewer reuse; no malloc hooks."""
from pathlib import Path
import hashlib, importlib.util, json, os, re, shlex, shutil, subprocess, sys, time, zipfile
HERE=Path(__file__).resolve().parent
PKG='com.d2rmobile.nativeport'
R=Path('/home/dl/d2r-mobile')

def load(path):
    spec=importlib.util.spec_from_file_location('official_native_heap_viewer',path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def proof(p,pinned,read,require):
    cfg=p['malloc_debug']
    require(set(cfg)=={'packaging','mode','runtime_symbols','driver_symbols'},'malloc debug fields differ')
    require(cfg['mode']=='control-only' and 'heap_profile' not in p,'one control diagnostic only; no concurrent heapprofd')
    pack=read(pinned(cfg['packaging']))
    require(pack['stage']=='DIAGNOSTIC_APK_REPACKAGED_EXACT_BASE_PAYLOAD_PLUS_OFFICIAL_ENV_WRAP','diagnostic packaging receipt missing')
    require(pack['APK']['sha256']==p['new_apk'] and pack['runtime_commit']==p['runtime_commit'],'diagnostic target differs')
    base=read(pinned(pack['base_canonical']))
    require(base['commit']==p['runtime_commit'] and base['canonical_native_library_sha256']==pack['six_SO'],'native sources differ')
    for row in [pack['source'],*pack['tool_inputs']]:pinned(row)
    actual_apk=pinned(pack['APK']);base_apk=pinned(pack['base_APK'])
    def entries(path):
        with zipfile.ZipFile(path) as z:
            require(len(z.namelist())==len(set(z.namelist())),'duplicate APK entry')
            return {n:hashlib.sha256(z.read(n)).hexdigest()for n in z.namelist()if not n.startswith('META-INF/')}
    old=entries(base_apk);new=entries(actual_apk)
    require(old==pack['original_payload_entries'] and new==pack['diagnostic_payload_entries'],'packaged input payload changed')
    require(new=={**old,'lib/arm64-v8a/wrap.sh':pack['wrap_sha256']},'other than wrapper changed')
    with zipfile.ZipFile(actual_apk)as z:
        wrap=z.read('lib/arm64-v8a/wrap.sh')
    require(hashlib.sha256(wrap).hexdigest()==pack['wrap_sha256'],'wrapper changed')
    require(pack['dump_prefix']=='/data/user/0/'+PKG+'/files/runtime/temp/malloc-debug-control','unknown dump destination')
    expected='backtrace=16 backtrace_full verbose backtrace_dump_prefix='+pack['dump_prefix']
    require(pack['malloc_options']==expected and ('export LIBC_DEBUG_MALLOC_OPTIONS="'+expected+'"').encode()in wrap and b'LD_PRELOAD'not in wrap,'unexpected official options')
    runtime=pinned(cfg['runtime_symbols']);driver=pinned(cfg['driver_symbols'])
    require(cfg['runtime_symbols']['sha256']==p['new_elf'] and cfg['driver_symbols']['sha256']==p['driver']['sha256'],'diagnostic symbols differ')
    require('counter_snapshot'not in p,'first malloc control diagnostic deliberately omits concurrent debugger')
    cfg['dump_prefix']=pack['dump_prefix'];cfg['malloc_options']=expected
    cfg['runtime_symbols_path']=str(runtime);cfg['driver_symbols_path']=str(driver)
    cfg['viewer']=str(HERE/'official/native_heapdump_viewer.compat.py')
    # The official parser expects GNU objdump's File-off column; LLVM
    # objdump omits it and would silently symbolize the wrong .text offset;
    # GNU addr2line also supplies the official viewer's -j option.
    cfg['symbol_tools']={'objdump':str(Path('/usr/bin/objdump').resolve()),'addr2line':str(Path('/usr/bin/addr2line').resolve())}
    upstream=(HERE/'official/native_heapdump_viewer.upstream.py').read_text()
    changed=upstream.replace('if len(parts) > 7 and parts[0] == "z" and parts[2] == "sz":','if len(parts) >= 7 and parts[0] == "z" and parts[2] == "sz":').replace('subprocess.check_output(["objdump", "-w", "-j", ".text", "-h", sofile])','subprocess.check_output(["objdump", "-w", "-j", ".text", "-h", sofile], text=True)').replace('stdout=subprocess.PIPE, stdin=subprocess.PIPE)','stdout=subprocess.PIPE, stdin=subprocess.PIPE, text=True)')
    require(Path(cfg['viewer']).read_text()==changed,'official viewer compatibility delta changed')
    files=[Path(__file__),Path(pack['source']['path']),*HERE.glob('official/native_heapdump_viewer*'),*[Path(v).resolve()for v in cfg['symbol_tools'].values()]]
    return {str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files if f.is_file()}

def coverage(path,viewer):
    text=Path(path).read_text()
    if not text.startswith('Android Native Heap Dump v1.2\n')or not text.endswith('\nEND\n'):raise RuntimeError('official complete v1.2 dump/END absent')
    headers={key:int(re.search('^'+re.escape(label)+r': (\d+)\s*$',text,re.M)[1])for key,label in [('bytes','Total memory'),('records','Allocation records'),('frames','Backtrace size')]}
    if headers['frames']!=16:raise RuntimeError('wrong backtrace option')
    v=load(viewer);bt,maps=v.ParseNativeHeap(str(path),False,v.GetNumFieldValid(str(path)),'')
    total=sum(b.size*b.num_allocs for b in bt)
    if len(bt)!=headers['records']or total!=headers['bytes']or not maps or not total:raise RuntimeError('official parser/header/map totals disagree')
    result={'total_requested_live_bytes':total,'allocation_records':len(bt),'empty_stack_bytes':0,'one_frame_stack_bytes':0,'short_stack_under4_bytes':0,'host_or_driver_stack_bytes':0,'no_host_or_driver_stack_bytes':0}
    for b in bt:
        size=b.size*b.num_allocs
        if not b.frames:result['empty_stack_bytes']+=size
        if len(b.frames)==1:result['one_frame_stack_bytes']+=size
        if len(b.frames)<4:result['short_stack_under4_bytes']+=size
        named=[]
        for pc in b.frames:
            m=v.FindMapping(maps,pc)
            if m:named.append(m.name)
        relevant=any(Path(n).name in ('libdmhost_android.so','libvulkan_freedreno.so')for n in named)
        result['host_or_driver_stack_bytes'if relevant else'no_host_or_driver_stack_bytes']+=size
    result['retained_bytes_are_not_RSS']=True
    return result,maps

def collect(dev,out,cfg,pid,uid,journal):
    out=Path(out);out.mkdir();state={'accepted':False,'stage':'prepared','cleanup_confirmed':True,'errors':[],'game_PID':int(pid),'ordinary_UID':int(uid),'no_external_collector':True}
    save=lambda:journal(dict(state))
    remote=cfg['dump_prefix']+'.'+pid+'.txt';state['remote_dump']=remote;save()
    local=out/'native-heap.txt'
    def own():
        current=dev.adb('shell','pidof '+PKG+' || true')
        if current!=pid:raise RuntimeError('unknown/replacement game PID; no signal')
        status=dev.app('cat','/proc/'+pid+'/status');ids=re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',status,re.M)
        tracer=re.search(r'^TracerPid:\s+(\d+)\s*$',status,re.M)
        if not ids or ids.groups()!=(str(uid),)*4 or not tracer or tracer[1]!='0':raise RuntimeError('ordinary UID/tracer mismatch; no signal')
        return status
    def exists():return dev.app('sh','-c','if test -f "$1"; then echo PRESENT; elif test -e "$1"; then exit 3; fi','sh',remote)
    try:
        status=own();(out/'owned-status.txt').write_text(status)
        maps=dev.app('cat','/proc/'+pid+'/maps');(out/'before-dump-maps.txt').write_text(maps)
        if 'libc_malloc_debug.so'not in maps:raise RuntimeError('official malloc_debug not loaded; do not send unregistered signal')
        actual_driver=dev.app('sha256sum','files/runtime/vulkan-driver/libvulkan_freedreno.so').split()[0]
        if actual_driver!=cfg['driver_symbols']['sha256']:raise RuntimeError('actual private driver differs')
        environ=dev.app('cat','/proc/'+pid+'/environ').split('\x00')
        if 'LIBC_DEBUG_MALLOC_OPTIONS='+cfg['malloc_options']not in environ:raise RuntimeError('exact malloc_debug startup environment absent')
        caught=re.search(r'^SigCgt:\s+([0-9a-fA-F]+)',status,re.M)
        if not caught or not(int(caught[1],16)&(1<<(47-1))):raise RuntimeError('SIG47 handler not caught; no signal')
        logs=dev.adb('shell','logcat','-b','main','-d','--pid='+pid,'-v','threadtime','-s','malloc_debug','libc')
        (out/'official-readiness-logcat.txt').write_text(logs)
        if not re.search(r"Run: 'kill -47 "+re.escape(pid)+r"' to dump the backtrace\.",logs):raise RuntimeError('official successful SIG47 registration receipt absent; no signal')
        if exists():raise RuntimeError('preexisting dump is not owned')
        own();dev.app('kill','-47',pid);state['owned_signal47_requested']=True;save()
        deadline=time.monotonic()+25
        while True:
            own()
            if exists() and dev.app('tail','-n','1',remote)=='END':break
            if time.monotonic()>=deadline:raise RuntimeError('official dump incomplete after bound; no repeat signal')
            time.sleep(.2)
        digest=dev.app('sha256sum',remote).split()[0];state['remote_sha256']=digest;save()
        # One original official export; never scans target payloads or invokes
        # allocator functions. exec-out writes directly to an owned host file.
        command=dev.command+['exec-out','run-as',PKG,'cat',remote]
        with local.open('wb')as f:subprocess.run(command,stdout=f,stderr=subprocess.PIPE,check=True,timeout=30)
        actual=hashlib.sha256(local.read_bytes()).hexdigest()
        if actual!=digest or dev.app('sha256sum',remote).split()[0]!=digest:raise RuntimeError('official dump changed during pull')
        state['dump_sha256']=actual;save()
        stats,parsed_maps=coverage(local,cfg['viewer']);state['coverage']=stats
        if not stats['host_or_driver_stack_bytes']:raise RuntimeError('dump has no authenticated host/driver frames; no attribution claim')
        state['dump_complete']=True;state['viewer_pending_after_game_stop']=True
        state['accepted']=True;state['stage']='official-malloc-debug-export-complete-viewer-pending';save()
    except BaseException as error:
        state['errors'].append(repr(error));state['stage']='malloc-debug-tool-invalid-preserved';save()
        # Keep our incomplete official output as untrusted failure evidence;
        # never signal again, read an unknown process, or accept this partial.
        if state.get('owned_signal47_requested') and not local.exists():
            try:
                own()
                if exists():
                    command=dev.command+['exec-out','run-as',PKG,'cat',remote]
                    with (out/'native-heap-INCOMPLETE-untrusted.txt').open('wb') as f:
                        subprocess.run(command,stdout=f,stderr=subprocess.PIPE,check=True,timeout=10)
                    state['incomplete_dump_sha256']=hashlib.sha256((out/'native-heap-INCOMPLETE-untrusted.txt').read_bytes()).hexdigest()
            except Exception as export_error:state['errors'].append('incomplete-export:'+repr(export_error))
            save()
    finally:
        # No daemon/host sampler was started. The debug shim exits with the
        # owned game through the original lifecycle; exported incomplete
        # evidence remains explicitly untrusted and cannot pass acceptance.
        save()
    return state

def analyze_after_game_stop(out,cfg,state,journal):
    """Existing official viewer runs only after the owned game has stopped."""
    out=Path(out);local=out/'native-heap.txt'
    try:
        stats,parsed_maps=coverage(local,cfg['viewer']);state['coverage']=stats
        symbols=out/'symbols';symbols.mkdir()
        for mapping in parsed_maps:
            name=Path(mapping.name).name
            if name not in ('libdmhost_android.so','libvulkan_freedreno.so'):continue
            if not mapping.name.startswith(('/data/app/','/data/data/'+PKG+'/','/data/user/0/'+PKG+'/'))or '..'in Path(mapping.name).parts:raise RuntimeError('unexpected app/driver mapping')
            source=Path(cfg['runtime_symbols_path']if name=='libdmhost_android.so'else cfg['driver_symbols_path'])
            dst=symbols/mapping.name.lstrip('/');dst.parent.mkdir(parents=True,exist_ok=True)
            if not dst.exists():shutil.copyfile(source,dst)
        tools=out/'symbol-tools';tools.mkdir()
        for name,path in cfg['symbol_tools'].items():(tools/name).symlink_to(path)
        env=os.environ.copy();env['PATH']=str(tools)+os.pathsep+env.get('PATH','')
        for label,extra in [('forward',[]),('reverse',['--reverse'])]:
            command=[sys.executable,cfg['viewer'],'--symbols',str(symbols),*extra,str(local)]
            with(out/('official-viewer-'+label+'.txt')).open('w')as f:
                subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,env=env,check=True,timeout=45)
            output=(out/('official-viewer-'+label+'.txt')).read_text()
            if 'exception while resolving symbols' in output or 'error: unknown argument' in output:raise RuntimeError('official symbolizer reported failure')
        state['accepted']=True;state['viewer_pending_after_game_stop']=False
        state['stage']='official-malloc-debug-dump-and-viewer-complete'
    except BaseException as error:
        state['accepted']=False;state['errors'].append(repr(error))
        state['stage']='official-malloc-debug-viewer-failed-dump-preserved'
    journal(dict(state));return state
