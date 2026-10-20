#!/usr/bin/env python3
"""Explicit diagnostic-only wrapping of a canonical APK; native/game bytes unchanged."""
import argparse, hashlib, json, re, shutil, subprocess, zipfile
from pathlib import Path

R=Path('/home/dl/d2r-mobile')
PKG='com.d2rmobile.nativeport'
PREFIX='/data/user/0/'+PKG+'/files/runtime/temp/malloc-debug-control'
WRAP=('#!/system/bin/sh\n'
      '# Official libc malloc_debug diagnostic; original TLSdesc native libraries unchanged.\n'
      'export LIBC_DEBUG_MALLOC_OPTIONS="backtrace=16 backtrace_full verbose backtrace_dump_prefix='+PREFIX+'"\n'
      'exec "$@"\n').encode()
SO={'libdmhost_android.so','libadrenotools.so','libfile_redirect_hook.so','libgsl_alloc_hook.so','libhook_impl.so','libmain_hook.so'}
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

def entries(path):
    with zipfile.ZipFile(path) as z:
        names=z.namelist()
        if len(names)!=len(set(names)):raise RuntimeError('duplicate ZIP entry')
        return {n:hashlib.sha256(z.read(n)).hexdigest() for n in names
                if not n.startswith('META-INF/')}

def signer(apksigner,path):
    p=subprocess.run([str(apksigner),'verify','--print-certs',str(path)],check=True,text=True,capture_output=True)
    ids=re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-f]+)',p.stdout)
    if len(ids)!=1:raise RuntimeError('one exact signer required')
    return ids[0]

def build(args):
    base_ready=json.loads(args.canonical.read_text())
    if sha(args.canonical)!=args.canonical_sha256:raise RuntimeError('canonical ready changed')
    base=Path(base_ready['canonical_APK']['archive']['path'])
    if sha(base)!=base_ready['canonical_APK_sha256']:raise RuntimeError('base APK changed')
    original=entries(base)
    if any(n.endswith('/wrap.sh') for n in original):raise RuntimeError('base already wrapped')
    libs={Path(n).name:h for n,h in original.items() if n.startswith('lib/') and n.endswith('.so')}
    if set(libs)!=SO or libs!=base_ready['canonical_native_library_sha256']:raise RuntimeError('canonical six SO mismatch')
    if any(re.search(r'(^|/)(main|rtld|subsdk\d*)\.(bin|nso)$',n) for n in original):raise RuntimeError('game module inside APK')
    plan={'diagnostic_only':True,'base_canonical':{'path':str(args.canonical.resolve()),'sha256':sha(args.canonical)},
          'base_APK':{'path':str(base),'sha256':sha(base)},'runtime_commit':base_ready['commit'],
          'six_SO':libs,'only_added_payload':'lib/arm64-v8a/wrap.sh','wrap_sha256':hashlib.sha256(WRAP).hexdigest(),
          'malloc_options':'backtrace=16 backtrace_full verbose backtrace_dump_prefix='+PREFIX,
          'dump_prefix':PREFIX,'output':str(args.output.resolve()),'canonical_build_record_base':base_ready.get('canonical_build_manifest'),
          'source':{'path':str(Path(__file__).resolve()),'sha256':sha(__file__)},'no_Cmake_or_runtime_rebuild':True}
    if not args.build:return plan
    args.output.mkdir(parents=True,exist_ok=False)
    sdk=R/'.local/android-sdk';bt=sdk/'build-tools/36.0.0';key=R/'.local/build/android-apk/debug.keystore'
    if not key.is_file():raise RuntimeError('existing canonical signer key missing')
    unsigned=args.output/'unsigned.apk'
    with zipfile.ZipFile(base) as src,zipfile.ZipFile(unsigned,'w') as dst:
        for info in src.infolist():
            if info.filename.startswith('META-INF/'):continue
            dst.writestr(info,src.read(info.filename))
        info=zipfile.ZipInfo('lib/arm64-v8a/wrap.sh');info.compress_type=zipfile.ZIP_DEFLATED
        info.external_attr=(0o100755<<16);dst.writestr(info,WRAP)
    subprocess.run([str(bt/'zipalign'),'-f','-P','16','4',str(unsigned),str(args.output/'aligned.apk')],check=True)
    apk=args.output/'malloc-debug-control.apk'
    subprocess.run([str(bt/'apksigner'),'sign','--ks',str(key),'--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android','--out',str(apk),str(args.output/'aligned.apk')],check=True)
    subprocess.run([str(bt/'zipalign'),'-c','-P','16','4',str(apk)],check=True)
    before_signer=signer(bt/'apksigner',base);after_signer=signer(bt/'apksigner',apk)
    if before_signer!=after_signer:raise RuntimeError('diagnostic signer differs')
    actual=entries(apk);want={**original,'lib/arm64-v8a/wrap.sh':hashlib.sha256(WRAP).hexdigest()}
    if actual!=want:raise RuntimeError('anything other than wrap changed')
    (args.output/'wrap.sh').write_bytes(WRAP)
    plan.update(stage='DIAGNOSTIC_APK_REPACKAGED_EXACT_BASE_PAYLOAD_PLUS_OFFICIAL_ENV_WRAP',
                APK={'path':str(apk),'sha256':sha(apk)},all_original_payload_entries_unchanged=True,
                payload_entry_count=len(original),signer_SHA256=after_signer,
                original_payload_entries=original,diagnostic_payload_entries=actual,
                tool_inputs=[{'path':str(p),'sha256':sha(p)}for p in (bt/'zipalign',bt/'apksigner',key)])
    (args.output/'packaging.json').write_text(json.dumps(plan,indent=2)+'\n')
    # Counter helper needs an exact APK/ELF binding, but this is explicitly a
    # packaging record whose unchanged ELF provenance remains the original.
    ready={'schema':1,'kind':'DIAGNOSTIC_WRAPPED_APK_UNCHANGED_CANONICAL_RUNTIME',
           'commit':base_ready['commit'],'base_canonical':plan['base_canonical'],
           'canonical_APK':{'archive':plan['APK']},'canonical_APK_sha256':plan['APK']['sha256'],
           'canonical_native_library_sha256':libs,
           'canonical_host_ELF_build_id':base_ready['canonical_host_ELF_build_id'],
           'packaging':{'path':str(args.output/'packaging.json'),'sha256':sha(args.output/'packaging.json')},
           'no_game_module_or_native_rewrite':True,'not_runtime_build':True}
    (args.output/'diagnostic-canonical-ready.json').write_text(json.dumps(ready,indent=2)+'\n')
    return plan

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--malloc-debug-wrap',action='store_true',required=True,help='explicitly opt into intrusive official malloc_debug diagnostic APK')
    p.add_argument('--canonical',type=Path,required=True);p.add_argument('--canonical-sha256',required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--build',action='store_true')
    a=p.parse_args();a.canonical=a.canonical.resolve();a.output=a.output.resolve()
    if not a.output.is_relative_to(R/'.local/build'):raise RuntimeError('private diagnostic output required')
    print(json.dumps(build(a),indent=2))
