#!/usr/bin/env python3
"""Build a separate ordinary-UID, compile-only APK using the qualified loader.

The shadow FS is an actual observed draw partner. Reusing it with the second
VS tests compiler load combining, not that full HDR pipeline's performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess as S
import zipfile

R = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--driver-build', type=Path, required=True)
a = p.parse_args()
O = a.output.resolve()
O.mkdir(exist_ok=False, parents=True)
sdk = R / '.local/android-sdk'
build = sdk / 'build-tools/36.0.0'
ndk = sdk / 'ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64'
jar = sdk / 'platforms/android-36/android.jar'
driver_record = json.loads((a.driver_build / 'build.json').read_text())
driver = Path(driver_record['driver']['path'])
assert driver_record['complete'] and sha(driver) == driver_record['driver']['sha256']
assert sha(driver) == '12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5'
normal = R / '.local/build/internal-queue-present-v3/canonical.apk'
assert sha(normal) == '28caae83d9b5e05a1b175c4c1989964b995061fbba7aed3537c980016f7acdbc'
base = R / '.local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5'
audit = R / '.local/logs/robustness-buffer-access-audit-20261028'
source = O / 'source'
source.mkdir()
shutil.copyfile(HERE / 'probe.c', source / 'probe.c')
original = R / 'tools/experiments/surface-control-ahb/fixture'
original_c = (original / 'probe.c').read_text()
start = original_c.index('  REQUIRE(snprintf(driver_root,')
end = original_c.index('volkInitializeCustom(get);', start) + len('volkInitializeCustom(get);')
(source / 'loader.inc').write_text(original_c[start:end] + '\n')
java = (original / 'ProbeActivity.java').read_text()
java = java.replace('package org.kxn.scahb;', 'package org.kxn.ubocompile;')
java = java.replace('loadLibrary("scahbprobe")', 'loadLibrary("ubocompile")')
java = java.replace('SurfaceControl AHB • API36 fixture • no game',
                    'UBO shader compilation only • no game or benchmark')
needle = '        SurfaceView view = new SurfaceView(this);'
assert needle in java
java = java.replace(needle, '''        try {
            for (String leaf : getAssets().list("shaders")) {
                try (InputStream in = getAssets().open("shaders/" + leaf);
                     FileOutputStream out = new FileOutputStream(new File(getFilesDir(), leaf))) {
                    byte[] block = new byte[65536]; int n;
                    while ((n = in.read(block)) != -1) out.write(block, 0, n);
                }
            }
        } catch (Exception e) { throw new IllegalStateException(e); }
''' + needle)
(source / 'ProbeActivity.java').write_text(java)
manifest = (original / 'AndroidManifest.xml').read_text().replace('org.kxn.scahb', 'org.kxn.ubocompile')
manifest = manifest.replace('SurfaceControl AHB Fixture', 'D2R Mobile')
(source / 'AndroidManifest.xml').write_text(manifest)
commands = []


def run(*parts):
    command = [str(x) for x in parts]
    commands.append(command)
    S.run(command, check=True)


cases = []
payloads = {'shadow.spv': base / 'd3678c2bd7da227d.spv'}
assert sha(payloads['shadow.spv']) == '66881d6753c6c7dca83a7550f338791d61029b28edd2a276c8de30503d687284'
for index, (key, directory) in enumerate([('3deaf68fd65b5298', '3deaf-ready-v1'),
                                         ('54c93fa22abff9ee', '54c-ready-v1')]):
    record = json.loads((audit / directory / 'result.json').read_text())
    proof = json.loads((audit / directory / 'equivalence-proof.json').read_text())
    assert record['complete'] and record['changed_shift_results'] == 48
    assert proof['all_changed_results_proved'] and len(proof['results']) == 48
    assert sha(base / (key + '.spv')) == record['input']['sha256']
    reflection = json.loads(S.check_output(['spirv-cross', str(base / (key + '.spv')), '--reflect']))
    assert {(u['set'], u['binding']) for u in reflection['ubos']} == {(0, 1), (0, 2), (0, 3)}
    assert not any(reflection.get(x) for x in ['ssbos', 'textures', 'separate_images', 'separate_samplers'])
    attrs = []
    for n, value in enumerate(sorted(reflection['inputs'], key=lambda x: x['location'])):
        assert value['type'] in ('vec4', 'uvec4') and n < 8
        attrs.append(f'{{.location={value["location"]},.binding=0,.format=' +
                     ('VK_FORMAT_R32G32B32A32_UINT' if value['type'] == 'uvec4'
                      else 'VK_FORMAT_R32G32B32A32_SFLOAT') + f',.offset={n * 16}}}')
    (source / (key + '.reflection.json')).write_text(json.dumps(reflection, indent=2) + '\n')
    for role, path in [('original', base / (key + '.spv')),
                       ('normalized', audit / directory / 'candidate.spv')]:
        leaf = f'case-{len(cases)}.spv'
        payloads[leaf] = path
        cases.append({'label': key + '-' + role, 'file': leaf, 'attributes': attrs,
                      'stride': 16 * len(attrs), 'input': str(path), 'sha256': sha(path),
                      'proof': str(audit / directory / 'equivalence-proof.json')})
fs_reflection = json.loads(S.check_output(['spirv-cross', str(payloads['shadow.spv']), '--reflect']))
assert {(u['set'], u['binding']) for u in fs_reflection['ubos']} == {(1, 0), (1, 1), (1, 2)}
assert not any(fs_reflection.get(x) for x in ['ssbos', 'textures', 'separate_images', 'separate_samplers'])
(source / 'shadow.reflection.json').write_text(json.dumps(fs_reflection, indent=2) + '\n')
(source / 'cases.h').write_text('struct compile_case {const char *label,*file;unsigned stride,count;VkVertexInputAttributeDescription attributes[8];};\nstatic const struct compile_case cases[4]={\n' +
    ',\n'.join('{.label="'+c['label']+'",.file="'+c['file']+'",.stride='+str(c['stride'])+',.count='+str(len(c['attributes']))+',.attributes={'+','.join(c['attributes'])+'}}' for c in cases) + '\n};\n')
run(ndk / 'bin/aarch64-linux-android29-clang', '-std=c11', '-O2', '-Wall', '-Wextra',
    '-Werror', '-D_DEFAULT_SOURCE', '-pthread', '-fPIC', '-shared',
    '-I', R / 'dm/third_party/volk', '-I', R / 'dm/third_party/libadrenotools/include',
    '-I', R / 'dm/host/android', '-I', source, source / 'probe.c',
    R / 'dm/third_party/volk/volk.c', '-landroid', '-ldl', '-Wl,-z,max-page-size=16384',
    '-o', O / 'libubocompile.so')
classes, dex = O / 'classes', O / 'dex'
classes.mkdir(); dex.mkdir()
run('javac', '--release', '8', '-classpath', jar, '-d', classes, source / 'ProbeActivity.java')
run(build / 'd8', '--lib', jar, '--min-api', '36', '--output', dex, *sorted(classes.rglob('*.class')))
run(build / 'aapt2', 'link', '-I', jar, '--manifest', source / 'AndroidManifest.xml', '-o', O / 'unsigned.apk')
hooks = ['libadrenotools.so', 'libmain_hook.so', 'libhook_impl.so',
         'libfile_redirect_hook.so', 'libgsl_alloc_hook.so']
with zipfile.ZipFile(normal) as archive, zipfile.ZipFile(O / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as apk:
    hook_hashes = {}
    for leaf in hooks:
        name = 'lib/arm64-v8a/' + leaf
        data = archive.read(name)
        hook_hashes[leaf] = hashlib.sha256(data).hexdigest()
        apk.writestr(name, data)
    apk.write(O / 'libubocompile.so', 'lib/arm64-v8a/libubocompile.so')
    apk.write(dex / 'classes.dex', 'classes.dex')
    apk.write(driver, 'assets/driver.so')
    apk.writestr('assets/driver.sha256', sha(driver))
    for leaf, path in payloads.items():
        apk.write(path, 'assets/shaders/' + leaf)
run(build / 'zipalign', '-f', '-P', '16', '4', O / 'unsigned.apk', O / 'aligned.apk')
apk = O / 'compile-probe.apk'
run(build / 'apksigner', 'sign', '--ks', R / '.local/build/device-probe/debug.keystore',
    '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
    '--out', apk, O / 'aligned.apk')
run(build / 'apksigner', 'verify', '--verbose', apk)
run(build / 'zipalign', '-c', '-P', '16', '4', apk)
inputs = [Path(__file__), HERE / 'probe.c', original / 'probe.c', original / 'ProbeActivity.java',
          original / 'AndroidManifest.xml', R / 'dm/host/android/stderr_redirect.h',
          R / 'dm/third_party/volk/volk.h', R / 'dm/third_party/volk/volk.c', *source.iterdir()]
record = {'complete': True, 'APK': {'path': str(apk), 'sha256': sha(apk)},
          'package': 'org.kxn.ubocompile', 'activity': '.ProbeActivity',
          'driver': driver_record['driver'], 'driver_build_sha256': sha(a.driver_build / 'build.json'),
          'hooks_from_exact_normal_v3': hook_hashes, 'cases': cases,
          'shader_payloads': {n: {'path': str(v), 'sha256': sha(v)} for n, v in payloads.items()},
          'source_inputs': {str(x): sha(x) for x in inputs}, 'commands': commands,
          'compiler_binary': {'path': str((ndk / 'bin/clang').resolve()), 'sha256': sha((ndk / 'bin/clang').resolve())},
          'scope': 'Four actual Vulkan graphics pipeline creations, no draw/submit/timer. '
                   'Both VS pairs link to actual shadow FS; not full HDR performance or game acceptance.',
          'baseline_native_qualification': 'Must observe 48 scalar body LDC for originals before interpreting candidate combining.'}
(O / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
print(apk)
