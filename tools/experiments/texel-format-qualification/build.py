#!/usr/bin/env python3
"""Build an isolated ordinary-UID functional oracle; never contacts a device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
EXPECTED_DRIVER = '12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5'
REUSED = ('dm/gfx/vulkan/nvn_vk_loader.c', 'dm/gfx/vulkan/nvn_vk_loader.h',
          'dm/third_party/volk/volk.c', 'dm/third_party/volk/volk.h',
          'dm/third_party/libadrenotools/include/adrenotools/driver.h',
          'dm/third_party/libadrenotools/include/adrenotools/priv.h',
          'dm/host/dm.h', 'dm/host/android/stderr_redirect.h')
HOOKS = {'libadrenotools.so', 'libfile_redirect_hook.so', 'libgsl_alloc_hook.so',
         'libhook_impl.so', 'libmain_hook.so'}
SHADERS = (('generate.comp', 'generate_spv'), ('triangle.vert', 'triangle_spv'),
           ('compare.frag', 'compare_spv'), ('reduce.comp', 'reduce_spv'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def checked_shader_sources():
    origin = json.loads((HERE / 'shader-origin.json').read_text())
    require(set(origin['shaders']) == {n for n, _ in SHADERS}, 'incomplete fixed shader source set')
    for name, _ in SHADERS:
        require(sha(HERE / 'shaders' / name) == origin['shaders'][name]['source_sha256'],
                'fixed shader source changed: ' + name)
    return origin


def check_base_apk(path):
    path = Path(path)
    manifest = json.loads(path.with_suffix('.apk.build.json').read_text())
    require(sha(path) == manifest['apk_sha256'], 'base APK SHA mismatch')
    require(HOOKS <= manifest['native_library_sha256'].keys(), 'base APK lacks original loader hooks')
    with zipfile.ZipFile(path) as z:
        for name in HOOKS:
            require(hashlib.sha256(z.read('lib/arm64-v8a/' + name)).hexdigest() ==
                    manifest['native_library_sha256'][name], 'base loader hook mismatch: ' + name)
    return manifest


def shader_include(products):
    lines = ['// Generated from the fixed owned SPIR-V; no game shaders.\n#include <stdint.h>\n']
    for name, symbol in SHADERS:
        data = (products / (name + '.spv')).read_bytes()
        require(data and len(data) % 4 == 0, 'SPIR-V word alignment: ' + name)
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        lines.append('alignas(4) static const uint32_t ' + symbol + '[]={\n')
        for i in range(0, len(words), 8):
            lines.append(','.join(hex(x) + 'u' for x in words[i:i + 8]) + ',\n')
        lines.append('};\n')
    return ''.join(lines)


def build(args):
    output = args.output.resolve()
    require(not output.is_relative_to(HERE), 'generated binaries must stay outside the tracked experiment directory')
    origin = checked_shader_sources()
    manifest = check_base_apk(args.base_apk)
    require(sha(args.driver) == args.driver_sha256, 'selected driver SHA mismatch')
    inputs = {str(p.relative_to(ROOT)): sha(p) for p in HERE.rglob('*')
              if p.is_file() and p.suffix != '.pyc' and '__pycache__' not in p.parts}
    inputs.update({name: sha(ROOT / name) for name in REUSED})
    fingerprint = hashlib.sha256(json.dumps(inputs, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    output.mkdir(parents=True, exist_ok=False)
    products = output / 'out'; products.mkdir()
    app = output / 'app'; app.mkdir()
    commands = []

    def run(argv):
        argv = list(map(str, argv)); commands.append(argv)
        return subprocess.check_output(argv, stderr=subprocess.STDOUT, text=True)

    for name, _ in SHADERS:
        spv = products / (name + '.spv')
        run(['glslangValidator', '-V', '--target-env', 'vulkan1.1', '-Os', '-o', spv, HERE / 'shaders' / name])
        run(['spirv-val', '--target-env', 'vulkan1.1', spv])
        run(['spirv-dis', '--raw-id', spv, '-o', products / (name + '.spvasm')])
        require(sha(spv) == origin['shaders'][name]['historical_SPIRV_sha256'],
                'compiler output differs from the qualified fixed SPIR-V; review tool versions before a new qualification: ' + name)
    (products / 'shaders.inc').write_text(shader_include(products))
    # The new archive/build has a new identity. Historical results retain their
    # original d7c... fingerprint; do not write it into a rebuilt native module.
    (products / 'provenance.inc').write_text('static const char source_fingerprint[]="' + fingerprint + '";\n')
    ndk = args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin'
    cc, cxx = ndk / 'aarch64-linux-android29-clang', ndk / 'aarch64-linux-android29-clang++'
    for source, name in [('dm/gfx/vulkan/nvn_vk_loader.c', 'loader'), ('dm/third_party/volk/volk.c', 'volk')]:
        run([cc, '-std=gnu11', '-O2', '-fPIC', '-D_GNU_SOURCE', '-I', ROOT,
             '-c', ROOT / source, '-o', products / (name + '.o')])
    run([cxx, '-std=c++20', '-O2', '-fPIC', '-shared', '-static-libstdc++', '-Wall', '-Wextra', '-Werror',
         '-Wno-missing-field-initializers', '-I', ROOT, '-I', products,
         HERE / 'source/probe.cpp', HERE / 'source/entry.cpp', products / 'loader.o', products / 'volk.o',
         '-ldl', '-o', products / 'libtexelformatprobe.so'])
    tools = args.sdk / ('build-tools/' + args.build_tools)
    jar = args.sdk / ('platforms/android-' + args.android_api + '/android.jar')
    classes, dex = app / 'classes', app / 'dex'; classes.mkdir(); dex.mkdir()
    run(['javac', '-source', '8', '-target', '8', '-classpath', jar, '-d', classes, HERE / 'source/ProbeActivity.java'])
    run([tools / 'd8', '--min-api', '29', '--lib', jar, '--output', dex, *sorted(classes.rglob('*.class'))])
    run([tools / 'aapt2', 'link', '-I', jar, '--manifest', HERE / 'source/AndroidManifest.xml', '-o', app / 'unsigned.apk'])
    with zipfile.ZipFile(args.base_apk) as base, zipfile.ZipFile(app / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as target:
        target.write(dex / 'classes.dex', 'classes.dex')
        target.write(products / 'libtexelformatprobe.so', 'lib/arm64-v8a/libtexelformatprobe.so')
        for name in sorted(HOOKS):
            target.writestr('lib/arm64-v8a/' + name, base.read('lib/arm64-v8a/' + name))
    run([tools / 'zipalign', '-f', '-P', '16', '4', app / 'unsigned.apk', app / 'aligned.apk'])
    apk = output / 'candidate.apk'
    run([tools / 'apksigner', 'sign', '--ks', args.keystore, '--ks-key-alias', 'androiddebugkey',
         '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', apk, app / 'aligned.apk'])
    (output / 'apksigner-verify.txt').write_text(run([tools / 'apksigner', 'verify', '--verbose', apk]))
    xml = run([tools / 'aapt2', 'dump', 'xmltree', apk, '--file', 'AndroidManifest.xml'])
    (output / 'manifest-xmltree.txt').write_text(xml)
    require('com.d2rmobile.texelformatprobe' in xml and 'com.d2rmobile.texelformatprobe.ProbeActivity' in xml,
            'isolated package/activity mismatch')
    with zipfile.ZipFile(apk) as z:
        natives = {n.rsplit('/', 1)[-1]: hashlib.sha256(z.read(n)).hexdigest()
                   for n in z.namelist() if n.startswith('lib/') and n.endswith('.so')}
    require(set(natives) == HOOKS | {'libtexelformatprobe.so'}, 'unexpected native payload')
    for name in HOOKS:
        require(natives[name] == manifest['native_library_sha256'][name], 'repacked hook changed')
    shutil.copy2(args.driver, output / 'libvulkan_freedreno.so')
    for name, value in inputs.items():
        require(sha(ROOT / name) == value, 'source changed during build: ' + name)
    require(sha(args.base_apk) == manifest['apk_sha256'] and sha(args.driver) == args.driver_sha256,
            'binary input changed during build')
    tools_used = [ndk / 'clang-19', ndk / 'ld.lld', tools / 'aapt2', tools / 'zipalign', tools / 'apksigner',
                  Path(shutil.which('glslangValidator')), Path(shutil.which('spirv-val')), Path(shutil.which('spirv-dis'))]
    result = {'schema': 1, 'status': 'HOST_BUILT_DEVICE_UNTESTED', 'package': 'com.d2rmobile.texelformatprobe',
              'activity': 'com.d2rmobile.texelformatprobe.ProbeActivity', 'APK': {'path': str(apk), 'sha256': sha(apk)},
              'driver': {'path': str(output / 'libvulkan_freedreno.so'), 'sha256': args.driver_sha256},
              'base_hooks_APK': {'path': str(args.base_apk.resolve()), 'sha256': manifest['apk_sha256']},
              'native_libraries': natives, 'source_fingerprint': fingerprint, 'source_inputs': inputs,
              'shader_origin_sha256': sha(HERE / 'shader-origin.json'),
              'SPIRV_sha256': {name: sha(products / (name + '.spv')) for name, _ in SHADERS},
              'tool_sha256': {str(p): sha(p) for p in tools_used}, 'commands': commands,
              'default_run': False, 'no_game_modules': True, 'historical_result_reused': False,
              'scope': 'Owned current-driver FS typed RGBA32F vs uint4 raw/quiet correctness; not production float4, HDR or performance.'}
    (output / 'ready.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--base-apk', type=Path, required=True, help='frozen original SDK APK and adjacent .apk.build.json, only its five loader hooks are reused')
    p.add_argument('--driver', type=Path, required=True)
    p.add_argument('--driver-sha256', default=EXPECTED_DRIVER)
    p.add_argument('--sdk', type=Path, default=ROOT / '.local/android-sdk')
    p.add_argument('--ndk', type=Path, default=ROOT / '.local/android-sdk/ndk/28.2.13676358')
    p.add_argument('--build-tools', default='36.0.0')
    p.add_argument('--android-api', default='36')
    p.add_argument('--keystore', type=Path, default=ROOT / '.local/build/android-apk/debug.keystore')
    print(json.dumps(build(p.parse_args()), indent=2))


if __name__ == '__main__':
    main()
