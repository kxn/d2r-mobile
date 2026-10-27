#!/usr/bin/env python3
"""Build/run real Linux FD tests and compile their actual Android/Bionic binary.

No device access, application build, game execution or fdsan emulation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[4]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=ROOT / '.local/build/stderr-redirect-v1')
    parser.add_argument('--ndk', type=Path,
                        default=ROOT / '.local/android-sdk/ndk/28.2.13676358')
    parser.add_argument('--host-cc', default='gcc')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = Path(shutil.which(args.host_cc) or args.host_cc).resolve(strict=True)
    android_cc = (args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/'
                  'aarch64-linux-android29-clang').absolute()
    if not android_cc.is_file():
        raise SystemExit(f'Android API29 compiler missing: {android_cc}')
    sources = [ROOT / 'dm/host/android/stderr_redirect.h',
               ROOT / 'dm/host/android/tests/native_stderr_redirect.c',
               ROOT / 'dm/host/android/jni.c', Path(__file__).resolve()]
    source_hashes = {str(p.relative_to(ROOT)): sha(p) for p in sources}
    commands = []
    receipt = dict(schema=1, source_sha256=source_hashes,
                   application_rebuilt=False, device_accessed=False,
                   game_modules_executed=False,
                   android_owner_and_race_tests='compiled_not_executed',
                   host_real_fdsan_owner='unavailable_not_emulated',
                   commands=commands)

    def run(command, name, env=None):
        result = subprocess.run([str(x) for x in command],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, env=env)
        log = out / (name + '.log')
        log.write_text(result.stdout)
        commands.append(dict(argv=[str(x) for x in command], exit=result.returncode,
                             log=str(log), log_sha256=sha(log)))
        if result.returncode:
            (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
            raise SystemExit(f'{name} failed; retained {log}')

    run([cc, '--version'], 'host-compiler')
    run([android_cc, '--version'], 'android-compiler')
    common = ['-D_GNU_SOURCE', '-std=c11', '-g', '-Wall', '-Wextra',
              '-Werror', '-pthread']
    test = sources[1]
    host = out / 'native_stderr_redirect_host'
    san = out / 'native_stderr_redirect_san'
    android = out / 'native_stderr_redirect_android29'
    run([cc, *common, '-O2', test, '-o', host], 'host-build')
    run([host], 'host')
    run([cc, *common, '-O1', '-fsanitize=address,undefined',
         '-fno-omit-frame-pointer', '-no-pie', test, '-o', san], 'sanitizer-build')
    env = os.environ.copy()
    env['ASAN_OPTIONS'] = 'detect_leaks=1'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1'
    run([san], 'sanitizer', env=env)
    run([android_cc, *common, '-O2', '-Wl,-z,max-page-size=16384',
         test, '-o', android], 'android-build')
    if source_hashes != {str(p.relative_to(ROOT)): sha(p) for p in sources}:
        raise SystemExit('Source changed during verification; result not qualified')
    receipt.update(completed=True,
                   artifacts={str(p): sha(p) for p in [host, san, android]},
                   compiler_sha256={str(cc): sha(cc),
                                    str(android_cc): sha(android_cc)})
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(receipt=str(out / 'receipt.json'),
                          android_binary=str(android),
                          android_execution='pending',
                          game_modules_executed=False), indent=2))


if __name__ == '__main__':
    main()
