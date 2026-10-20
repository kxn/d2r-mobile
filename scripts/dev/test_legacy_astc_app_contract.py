#!/usr/bin/env python3
"""Offline wrapper/launcher/stdio contracts; no ADB, GPU, game or timing."""
import ast,hashlib,json,subprocess,tempfile
from pathlib import Path
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[2]
java=(ROOT/'dm/android/java/com/d2rmobile/nativeport/MainActivity.java').read_text()
jni=(ROOT/'dm/host/android/jni.c').read_text()
assert 'boolean memoryOnly, boolean validation);' in java
assert 'getBooleanExtra("vulkan_test_validation", true)' in java
chunk=jni.split('JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTests(',1)[1].split('// Separate owned test selector;',1)[0]
assert 'jboolean memory_only, jboolean validation_enabled)' in chunk
assert 'dm_fixture_output_open(file)' in chunk and 'freopen(' not in chunk
assert 'unsetenv("DM_VULKAN_VALIDATE")' in chunk and 'unsetenv("DM_VULKAN_SYNC_VALIDATE")' in chunk
assert 'native_vulkan_app_configuration' in chunk and 'native_vulkan_app_terminal' in chunk
launcher=ROOT/'scripts/dev/android-game-launch.py';tree=ast.parse(launcher.read_text())
choice=next(n for n in ast.walk(tree) if isinstance(n,ast.If) and ast.unparse(n.test)=='args.legacy_astc_copy is not None' and 'launch' in ast.unparse(n))
for value in (True,False):
 g={'args':SimpleNamespace(legacy_astc_copy=value),'launch':[]}
 exec(compile(ast.Module(body=[choice],type_ignores=[]),str(launcher),'exec'),g)
 assert g['launch']==['--ez','legacy_astc_copy',str(value).lower()]
for option in ('--legacy-astc-copy','--no-legacy-astc-copy'):
 p=subprocess.run(['python3',str(launcher),'--attach',option],capture_output=True,text=True)
 assert p.returncode==2 and 'startup-only' in p.stderr and 'Traceback' not in p.stderr
assert "'requested_legacy_astc_copy':args.legacy_astc_copy" in launcher.read_text()
# Exercise the exact existing shared output helper with independent FILE streams.
with tempfile.TemporaryDirectory(prefix='legacy-app-contract-') as td:
 d=Path(td);s=d/'stdio.c';s.write_text('#include "fixture_output.h"\nint main(int n,char **a){int e=dm_fixture_output_open(a[1]);if(e)return e;for(int k=0;k<64;k++){fprintf(stdout,"{\\"row\\":%d}\\n",k);fprintf(stderr,"diag %d\\n",k);}fprintf(stdout,"{\\"terminal\\":true}\\n");return 0;}\n')
 subprocess.run(['cc','-O3','-I',str(ROOT/'dm/host/android'),str(s),'-o',str(d/'stdio')],check=True)
 subprocess.run([str(d/'stdio'),str(d/'stdio.log')],check=True)
 lines=(d/'stdio.log').read_text().splitlines();assert len(lines)==129
 for k in range(64):assert json.loads(lines[2*k])=={'row':k} and lines[2*k+1]=='diag '+str(k)
 assert json.loads(lines[-1])=={'terminal':True}
result={'status':'passed','scope':'exact Java/JNI signature/default validation, launch cold true/false and attach rejection, real existing append stdio helper; no ADB/device',
 'stdio_lines_preserved':129,'renderer_changed':False,'cases':7}
print(json.dumps(result,indent=2))
