"""One ready-point read of existing own-process procfs; no timing or policy writes."""
from pathlib import Path
import hashlib,json

def collect(dev,pid,out,require):
    require(pid.isdecimal(),'owned PID required for scheduling snapshot')
    names=dev.app('sh','-c','for t in /proc/"$1"/task/*; do printf "%s\\t" "${t##*/}"; cat "$t/comm"; done','sh',pid)
    (out/'ready-thread-names.txt').write_text(names)
    parsed={}
    for line in names.splitlines():
        tid,sep,name=line.partition('\t')
        if sep and tid.isdecimal():parsed[tid]=name
    nvn=[t for t,n in parsed.items() if n=='nvn-translate']
    require(len(nvn)==1,'ready snapshot requires one actual NvN thread')
    selected={pid:'process-main/UI',nvn[0]:'nvn-translate'}
    for tid,name in parsed.items():
        if 'nativeport' in name and tid not in selected:selected[tid]='same-app-name; role not inferred'
    records=[]
    for tid,role in selected.items():
        for leaf in ('stat','status','sched','cgroup'):
            path='/proc/'+pid+'/task/'+tid+'/'+leaf
            text=dev.app('sh','-c','if test -r "$1"; then cat "$1"; else echo UNREADABLE; fi','sh',path)
            target=out/('ready-thread-'+tid+'-'+leaf+'.txt');target.write_text(text)
            records.append({'tid':int(tid),'comm':parsed.get(tid),'role':role,'proc_path':path,
                            'file':str(target),'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
                            'unreadable':text.strip()=='UNREADABLE'})
    result={'owned_PID':int(pid),'NvN_tid':int(nvn[0]),'records':records,
            'scope':'Single ready-point system procfs read outside official windows; preserve raw policy/nice/CPUallowed/cgroup/uclamp fields. No sampling, timer, policy change or inferred guest thread roles.'}
    (out/'ready-scheduling-snapshot.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
