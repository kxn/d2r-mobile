#!/usr/bin/env python3
# 构建原生 Android 调试外壳，打包原宿主库与启动预加载脚本，不含游戏文件。
# Build the native Android debug shell with the original host library and startup preload script, without game files.
import csv
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import zipfile
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--native-resources',action='store_true',help='compile default-OFF host-native texture protocol; requires shared pools')
parser.add_argument('--shared-pools',action='store_true',help='compile default-OFF fixed shared Pool/early guest heap experiment')
parser.add_argument('--native-jobs',type=int,choices=range(1,65),default=4,help='parallel native build jobs (default4)')
parser.add_argument('--tlsdesc', action='store_true', help='Native dynamic TLS, without wrap.sh; requires elfdesc game rewrites')
parser.add_argument('--opt-level', choices=['1', '2', '3'], default='3', help='native host optimization; preserves debug symbols and frame pointers')
parser.add_argument('--renderdoc-api', action='store_true', help='optional official application-API capture, not custom timing')
args = parser.parse_args()
if args.native_resources and not args.shared_pools:
    parser.error('--native-resources requires --shared-pools')
OUT = ROOT / '.local/build/android-apk'
SDK = ROOT / '.local/android-sdk'
BUILD = SDK / 'build-tools/36.0.0'
JAR = SDK / 'platforms/android-36/android.jar'
SOURCE = ROOT / 'dm/android'
OUT.mkdir(parents=True, exist_ok=True)
apk = OUT / ('d2r-native-tlsdesc-debug.apk' if args.tlsdesc else 'd2r-native-debug.apk')
manifest_path=apk.with_suffix(apk.suffix+'.build.json')
# A failed build must not leave a previous APK at the success output path.
# Accepted releases stay in their separate immutable archives.
apk.unlink(missing_ok=True)
manifest_path.unlink(missing_ok=True)

def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)

run('cmake', '-S', ROOT / 'dm/host/android', '-B', ROOT / '.local/build/android-native',
    '-DDM_ANDROID_TLS_DESC=' + ('ON' if args.tlsdesc else 'OFF'),
    '-DDM_VULKAN_SHARED_POOLS=' + ('ON' if args.shared_pools else 'OFF'),
    '-DDM_VULKAN_NATIVE_RESOURCES=' + ('ON' if args.native_resources else 'OFF'),
    '-DDM_ANDROID_RENDERDOC_API=' + ('ON' if args.renderdoc_api else 'OFF'),
    '-DDM_RENDERDOC_INCLUDE_DIR=' + str(ROOT/'.local/tools/Arm_Performance_Studio_2026.5/renderdoc_for_arm_gpus/include'),
    '-DDM_ANDROID_RUNTIME_OPT_LEVEL=' + args.opt_level)
run('cmake', '--build', ROOT / '.local/build/android-native', '-j'+str(args.native_jobs), '--target', 'dmhost_android')
rows = {row['键']: row for row in csv.DictReader((ROOT / 'dm/i18n/ui_text.tsv').open(), delimiter='\t')}
shutil.rmtree(OUT / 'res', ignore_errors=True)
for resource in sorted((SOURCE/'res').rglob('*')):
    if resource.is_file():
        destination=OUT/'res'/resource.relative_to(SOURCE/'res')
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(resource,destination)
for folder, language in [('values', 'English'), ('values-zh-rCN', '中文')]:
    directory = OUT / 'res' / folder
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'strings.xml').write_text('<resources><string name="app_name">' + escape(rows['dmAppTitle'][language]) + '</string></resources>\n')
classes, dex = OUT / 'classes', OUT / 'dex'
for intermediate in (classes, dex):
    # Deleted or renamed Java classes must not survive into a later APK.
    shutil.rmtree(intermediate, ignore_errors=True)
    intermediate.mkdir()
run('javac', '--release', '8', '-classpath', JAR, '-d', classes, *sorted((SOURCE / 'java').rglob('*.java')))
run(BUILD / 'd8', '--lib', JAR, '--min-api', '29', '--output', dex, *sorted(classes.rglob('*.class')))
run(BUILD / 'aapt2', 'compile', '--dir', OUT / 'res', '-o', OUT / 'resources.zip')
run(BUILD / 'aapt2', 'link', '-I', JAR, '--manifest', SOURCE / 'AndroidManifest.xml', '-o', OUT / 'unsigned.apk', OUT / 'resources.zip')
touch_assets = sorted((ROOT / 'dm/ios/Sources/TouchArt').glob('*.png'))
hud_assets = sorted(path for path in
    (ROOT / 'dm/ios/Gen/assets/NativeHUD/v4/hud_v4_ui').iterdir()
    if path.suffix in ('.png', '.json'))
with zipfile.ZipFile(OUT / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as package:
    package.write(ROOT / 'dm/third_party/opus/COPYING', 'assets/licenses/opus-1.5.2.txt')
    package.write(dex / 'classes.dex', 'classes.dex')
    package.write(ROOT / '.local/build/android-native/nvn_vk_bright_spv.json', 'assets/vulkan/bright-build.json')
    package.write(ROOT / '.local/build/android-native/nvn_vk_nofxaa_spv.json', 'assets/vulkan/nofxaa-build.json')
    for stage in ['vert', 'frag']:
        package.write(ROOT / f'.local/build/android-native/nvn_vk_present_{stage}_spv.json',
                      f'assets/vulkan/present-{stage}-build.json')
    for stage in ['vert', 'frag']:
        package.write(ROOT / f'.local/build/android-native/nvn_vk_bloom_quarter_{stage}_spv.json',
                      f'assets/vulkan/bloom-quarter-{stage}-build.json')
    for stage in ['vert', 'frag', 'masked', 'mask']:
        package.write(ROOT / f'.local/build/android-native/nvn_vk_extrap_{stage}_spv.json',
                      f'assets/vulkan/extrap-{stage}-build.json')
    for asset in touch_assets:
        package.write(asset, 'assets/touch/' + asset.name)
    for asset in hud_assets:
        package.write(asset, 'assets/hud_v4_ui/' + asset.name)
        if asset.name.startswith('sil_dm_native_') and asset.suffix == '.png':
            package.write(asset, 'assets/touch/' + asset.name)
    package.write(ROOT / '.local/build/android-native/libdmhost_android.so', 'lib/arm64-v8a/libdmhost_android.so')
    driver_libraries=sorted((ROOT/'.local/build/android-native/vulkan-loader-libs').glob('*.so'))
    if {p.name for p in driver_libraries}!={'libadrenotools.so','libmain_hook.so','libhook_impl.so',
                                           'libfile_redirect_hook.so','libgsl_alloc_hook.so'}:
        raise SystemExit('Vulkan driver loader libraries incomplete')
    for library in driver_libraries:
        package.write(library,'lib/arm64-v8a/'+library.name)
    if not args.tlsdesc:
        package.write(SOURCE / 'wrap.sh', 'lib/arm64-v8a/wrap.sh')
    layer = ROOT / '.local/tools/vulkan-validation/libVkLayer_khronos_validation.so'
    if layer.is_file():
        package.write(layer, 'lib/arm64-v8a/' + layer.name)
key = OUT / 'debug.keystore'
if not key.is_file():
    run('keytool', '-genkeypair', '-keystore', key, '-alias', 'androiddebugkey', '-storepass', 'android', '-keypass', 'android', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=D2R Local Development', '-noprompt')
run(BUILD / 'zipalign', '-f', '-P', '16', '4', OUT / 'unsigned.apk', OUT / 'aligned.apk')
apk = OUT / ('d2r-native-tlsdesc-debug.apk' if args.tlsdesc else 'd2r-native-debug.apk')
run(BUILD / 'apksigner', 'sign', '--ks', key, '--ks-key-alias', 'androiddebugkey', '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', apk, OUT / 'aligned.apk')
run(BUILD / 'apksigner', 'verify', apk)
run(BUILD / 'zipalign', '-c', '-P', '16', '4', apk)
native_build=ROOT/'.local/build/android-native'
dependency_text=subprocess.run(['ninja','-C',str(native_build),'-t','deps'],
                               text=True,capture_output=True,check=True).stdout
inputs=set()
for line in dependency_text.splitlines():
    if not line.startswith('    '):continue
    path=Path(line.strip())
    if not path.is_absolute():path=native_build/path
    path=path.resolve()
    if path.is_file() and (path.is_relative_to(ROOT/'dm') or
            (args.renderdoc_api and path.name=='renderdoc_app.h' and path.is_relative_to(ROOT/'.local/tools')) or
            path == native_build/'dm-opus/config.h' or
            (path.parent==native_build and path.name.startswith('nvn_vk_'))):inputs.add(path)
inputs.update(path for path in SOURCE.rglob('*') if path.is_file())
inputs.add(Path(__file__).resolve())
for name in ['volk','libadrenotools','opus']:
    inputs.update(p for p in (ROOT/'dm/third_party'/name).rglob('*') if p.is_file())
inputs.update([ROOT/'dm/host/android/CMakeLists.txt',ROOT/'dm/gfx/vulkan/CMakeLists.txt'])
inputs.update([ROOT/'dm/gfx/vulkan/builtins.cmake',ROOT/'dm/gfx/vulkan/build_builtin.py'])
inputs.update(p for p in (ROOT/'dm/gfx/vulkan/shaders').iterdir() if p.is_file())
inputs.update(native_build.glob('nvn_vk_*_spv.json'))
inputs.update(touch_assets)
inputs.update(hud_assets)
inputs.add(ROOT/'dm/ios/Sources/TouchArt/native_art_manifest.json')
opus_config=native_build/'dm-opus/config.h'
# The decoder's generated configuration is a compilation input. Record it
# alongside the pinned source, even when Ninja's dependency cache is empty.
inputs.update([ROOT/'dm/third_party/opus_fixed.cmake',
               ROOT/'scripts/dev/prepare-opus-source.py',opus_config])
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
opus_upstream=ROOT/'dm/third_party/opus/UPSTREAM.json'
opus_provenance={'upstream':json.loads(opus_upstream.read_text()),
                 'source_checksums_sha256':sha(ROOT/'dm/third_party/opus/SHA256SUMS'),
                 'cmake_rules_sha256':sha(ROOT/'dm/third_party/opus_fixed.cmake'),
                 'generated_config_sha256':sha(opus_config)}
manifest={'apk_sha256':sha(apk),'host_library_sha256':sha(native_build/'libdmhost_android.so'),
          'source_revision':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
          'tlsdesc':args.tlsdesc,'host_opt_level':args.opt_level,'contains_game_binary':False,
          'official_renderdoc_api':args.renderdoc_api,
          'fixed_shared_pools_compiled':args.shared_pools,
          'native_resources_compiled':args.native_resources,
          'native_jobs':args.native_jobs,
          'cmake_cache_sha256':sha(native_build/'CMakeCache.txt'),
          'opus_dependency':opus_provenance,
          'native_library_sha256':{p.name:sha(p) for p in
              [native_build/'libdmhost_android.so',*driver_libraries]},
          'upstream_ui_assets_sha256':{str(path.relative_to(ROOT)):sha(path)
              for path in [*touch_assets,*hud_assets]},
          'source_inputs':{str(path.relative_to(ROOT)):sha(path) for path in sorted(inputs)}}
manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
print(apk)
