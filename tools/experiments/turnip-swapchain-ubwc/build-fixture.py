#!/usr/bin/env python3
"""Build a separate WSI correctness APK; never modify the game package."""
import hashlib
import argparse
import json
from pathlib import Path
import subprocess as S
import zipfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'fixture'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--driver-build', type=Path, required=True)
args = parser.parse_args()
OUT = args.output.resolve()
OUT.mkdir(exist_ok=False)
SDK = ROOT / '.local/android-sdk'
BUILD = SDK / 'build-tools/36.0.0'
NDK = SDK / 'ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64'
VOLK = ROOT / 'dm/third_party/volk'
TOOLS = ROOT / 'dm/third_party/libadrenotools/include'
JAR = SDK / 'platforms/android-36/android.jar'
driver_record_path = args.driver_build.resolve() / 'build.json'
driver_record = json.loads(driver_record_path.read_text())
assert driver_record['complete']
driver = Path(driver_record['driver']['path'])
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(driver) == driver_record['driver']['sha256']
ordinary = ROOT / '.local/build/write-path-tiny-border-v1/canonical.apk'
assert sha(ordinary) == '54e724be74b6c227dd6f3dfa6f7a290a63de4396cee3cc285398e9d7835d50a7'
commands = []


def run(*parts):
    command = [str(x) for x in parts]
    commands.append(command)
    S.run(command, check=True)


run(NDK / 'bin/aarch64-linux-android29-clang', '-std=c11', '-O2',
    '-Wall', '-Wextra', '-Werror', '-D_DEFAULT_SOURCE', '-DVK_USE_PLATFORM_ANDROID_KHR',
    '-fPIC', '-shared', '-I', VOLK, '-I', TOOLS, SOURCE / 'probe.c', VOLK / 'volk.c',
    '-landroid', '-ldl', '-Wl,-z,max-page-size=16384', '-o', OUT / 'libwsiprobe.so')
classes, dex = OUT / 'classes', OUT / 'dex'
classes.mkdir(); dex.mkdir()
run('javac', '--release', '8', '-classpath', JAR, '-d', classes, SOURCE / 'ProbeActivity.java')
run(BUILD / 'd8', '--lib', JAR, '--min-api', '29', '--output', dex, *sorted(classes.rglob('*.class')))
run(BUILD / 'aapt2', 'link', '-I', JAR, '--manifest', SOURCE / 'AndroidManifest.xml', '-o', OUT / 'unsigned.apk')
with zipfile.ZipFile(ordinary) as previous, zipfile.ZipFile(OUT / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as apk:
    native = {n: hashlib.sha256(previous.read(n)).hexdigest() for n in previous.namelist()
              if n.startswith('lib/arm64-v8a/') and n.endswith('.so') and not n.endswith('/libdmhost_android.so')}
    for name in native:
        apk.writestr(name, previous.read(name))
    apk.write(OUT / 'libwsiprobe.so', 'lib/arm64-v8a/libwsiprobe.so')
    apk.write(dex / 'classes.dex', 'classes.dex')
    apk.write(driver, 'assets/driver.so')
    apk.writestr('assets/driver.sha256', sha(driver))
keystore = ROOT / '.local/build/device-probe/debug.keystore'
assert keystore.is_file()
run(BUILD / 'zipalign', '-f', '-P', '16', '4', OUT / 'unsigned.apk', OUT / 'aligned.apk')
apk = OUT / 'wsi-fixture.apk'
run(BUILD / 'apksigner', 'sign', '--ks', keystore, '--ks-key-alias', 'androiddebugkey',
    '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', apk, OUT / 'aligned.apk')
run(BUILD / 'apksigner', 'verify', '--verbose', apk)
run(BUILD / 'zipalign', '-c', '-P', '16', '4', apk)
record = {'complete': True, 'APK': str(apk), 'APK_sha256': sha(apk), 'driver': driver_record['driver'],
          'driver_build_record_sha256': sha(driver_record_path),
          'reused_hooks': native, 'commands': commands,
          'source_revision': S.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
          'source_inputs': {str(p.relative_to(ROOT)): sha(p) for p in
                            [Path(__file__).resolve(), VOLK / 'volk.c', VOLK / 'volk.h', *SOURCE.iterdir()]},
          'scope': 'Independent Android WSI allocation/render/present/lifecycle correctness, no game, CPU simulation or profiler.'}
(OUT / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
print(apk, flush=True)
