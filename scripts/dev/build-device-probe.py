#!/usr/bin/env python3
"""Build the same native probe for Linux and an installable ARM64 Android APK."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/device-probe'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--renderdoc', action='store_true', help='optional official RenderDoc application-API smoke test, not a profiler implementation')
args = parser.parse_args()
OUT = ROOT / ('.local/build/device-probe-renderdoc' if args.renderdoc else '.local/build/device-probe')
OUT.mkdir(parents=True, exist_ok=True)
SDK = ROOT / '.local/android-sdk'
BUILD = SDK / 'build-tools/36.0.0'
NDK = SDK / 'ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64'
ANDROID_JAR = SDK / 'platforms/android-36/android.jar'


def run(*command):
    subprocess.run([str(part) for part in command], check=True)


run('glslangValidator', '-V', '--target-env', 'vulkan1.0', SOURCE / 'probe.comp', '-o', OUT / 'probe.spv')
run('spirv-val', '--target-env', 'vulkan1.0', OUT / 'probe.spv')
shader = (OUT / 'probe.spv').read_bytes()
words = struct.unpack('<' + 'I' * (len(shader) // 4), shader)
(OUT / 'probe_spv.h').write_text('static const uint32_t probe_spv[] = {' + ','.join(hex(word) for word in words) + '};\n')
flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', OUT]
if args.renderdoc:
    flags += ['-DDM_RENDERDOC_PROBE=1', '-I', ROOT / '.local/tools/Arm_Performance_Studio_2026.5/renderdoc_for_arm_gpus/include']
run('clang', *flags, SOURCE / 'probe.c', '-lvulkan', '-o', OUT / 'device-probe-linux')
run(NDK / 'bin/aarch64-linux-android28-clang', *flags, '-fPIC', '-shared', SOURCE / 'probe.c',
    '-lvulkan', '-ldl', '-Wl,-z,max-page-size=16384', '-o', OUT / 'libdeviceprobe.so')
classes, dex = OUT / 'classes', OUT / 'dex'
classes.mkdir(exist_ok=True)
dex.mkdir(exist_ok=True)
run('javac', '--release', '8', '-classpath', ANDROID_JAR, '-d', classes,
    SOURCE / 'android/com/d2rmobile/devprobe/ProbeActivity.java')
run(BUILD / 'd8', '--lib', ANDROID_JAR, '--min-api', '28', '--output', dex, *sorted(classes.rglob('*.class')))
run(BUILD / 'aapt2', 'link', '-I', ANDROID_JAR, '--manifest', SOURCE / 'android/AndroidManifest.xml',
    '-o', OUT / 'unsigned.apk')
with zipfile.ZipFile(OUT / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as apk:
    apk.write(dex / 'classes.dex', 'classes.dex')
    apk.write(OUT / 'libdeviceprobe.so', 'lib/arm64-v8a/libdeviceprobe.so')
keystore = ROOT / '.local/build/device-probe/debug.keystore'
if not keystore.exists():
    run('keytool', '-genkeypair', '-keystore', keystore, '-alias', 'androiddebugkey',
        '-storepass', 'android', '-keypass', 'android', '-keyalg', 'RSA', '-keysize', '2048',
        '-validity', '10000', '-dname', 'CN=D2R Local Development', '-noprompt')
run(BUILD / 'zipalign', '-f', '-P', '16', '4', OUT / 'unsigned.apk', OUT / 'aligned.apk')
apk_path = OUT / 'd2r-device-probe.apk'
run(BUILD / 'apksigner', 'sign', '--ks', keystore, '--ks-key-alias', 'androiddebugkey',
    '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', apk_path, OUT / 'aligned.apk')
run(BUILD / 'apksigner', 'verify', '--verbose', apk_path)
run(BUILD / 'zipalign', '-c', '-P', '16', '4', apk_path)
report = {'apk': str(apk_path), 'apk_sha256': hashlib.sha256(apk_path.read_bytes()).hexdigest(),
          'renderdoc_application_api': args.renderdoc,
          'source_inputs': {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__).resolve(), *sorted(p for p in SOURCE.rglob('*') if p.is_file())]},
          'abi': 'arm64-v8a', 'min_android_api': 28, 'target_android_api': 36,
          'native_elf_max_page_size': 16384, 'scope': 'capability probe; not the game port'}
(OUT / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
