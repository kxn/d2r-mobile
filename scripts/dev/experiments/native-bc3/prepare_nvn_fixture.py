#!/usr/bin/env python3
"""Freeze/build the original NVN oracle with two real BC3 packs; never use ADB."""
import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
DRIVER = '0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024'
PRODUCER = '9a8a5a64bc4dc8b9e19fb35d2ff1c0d0bd72f307f2e772f8a18e4e9d6f99019b'
PACKAGE = 'com.d2rmobile.nativefamilyfixture'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record(path):
    return {'path': str(Path(path).resolve()), 'sha256': sha(path)}


def native_fixture_entry(text, resident_lazy=False):
    before = 'if(setenv("DM_VULKAN_SHARED_POOLS","1",1) || setenv("PANVK_DM_HOST_IMPORT","true",1) ||'
    after = before + '\n       setenv("TU_DM_HOST_IMPORT","true",1) ||\n       setenv("DM_SHARED_GUEST_HEAP_FIXTURE","1",1) ||'
    if resident_lazy:
        # Only request the actual exact-provider contract, never a test override.
        after = '''if(setenv("DM_NATIVE_RESOURCE_EXPECT_RESIDENT","1",1) ||
       setenv("DM_NATIVE_RESOURCE_EXPECT_LAZY","1",1) ||
       setenv("DM_VK_SHARED_LAZY_WINDOWS","1",1))die("Native resource resident/lazy oracle cold setup");
    vk_pool_window_bytes(65536);
    ''' + after
    if text.count(before) != 1:
        raise ValueError('Native-resource fixture opt-in anchor changed')
    return text.replace(before, after)


def prepare(out, resident_lazy=False):
    if out.exists():
        raise ValueError('Use a new output; preserve previous/failed candidates')
    out.mkdir(parents=True)
    source = out / 'source'
    source.mkdir()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    archive = subprocess.check_output(['git', 'archive', revision], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(source, filter='data')
    # Capture the real current tracked source, including the minimal uncommitted
    # BC3 protocol/fixture changes; the ledger, not ambient HEAD, is authority.
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    for name in filter(None, names):
        original, target = ROOT / name, source / name
        if original.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, target)
        elif not original.exists() and target.is_file():
            target.unlink()
    manifest = source / 'dm/android/AndroidManifest.xml'
    text = manifest.read_text()
    for old, new in [('package="com.d2rmobile.nativeport"', 'package="' + PACKAGE + '"'),
                     ('android:name=".MainActivity"', 'android:name="com.d2rmobile.nativeport.MainActivity"')]:
        if text.count(old) != 1:
            raise ValueError('Manifest anchor changed: ' + old)
        text = text.replace(old, new)
    manifest.write_text(text)
    # The original PanVK test entry returns before the game's TU environment
    # setup. Reuse the already-qualified Turnip NVN fixture's cold opt-in only
    # inside this isolated source; never bypass driver capability/byte gates.
    jni = source / 'dm/host/android/jni.c'
    jni.write_text(native_fixture_entry(jni.read_text(), resident_lazy))
    # These generated upstream UI assets and SDK inputs are read-only copies or
    # links, never a dependency on mutable output in the live build directory.
    shutil.copytree(ROOT / 'dm/ios/Gen/assets', source / 'dm/ios/Gen/assets', dirs_exist_ok=True)
    # Reuse unchanged private generated tables from the real current build.
    # Otherwise archive mtimes force a costly full bright-level rescan unrelated
    # to this format-only fixture. Both source rules and bytes enter the ledger.
    for name in ['dm_vault_tab.h', 'dm_hud_exp.h', 'dm_bright_levels.inc']:
        shutil.copy2(ROOT / 'dm/host' / name, source / 'dm/host' / name)
        os.utime(source / 'dm/host' / name, None)
    local = source / '.local'
    local.mkdir()
    for name in ['android-sdk', 'venv']:
        (local / name).symlink_to(ROOT / '.local' / name, target_is_directory=True)
    keys = local / 'build/android-apk'
    keys.mkdir(parents=True)
    shutil.copy2(ROOT / '.local/build/android-apk/debug.keystore', keys / 'debug.keystore')
    driver_build = ROOT / '.local/build/turnip-native-bc3-v1/build.json'
    build = json.loads(driver_build.read_text())
    if not build['complete'] or sha(build['driver']['path']) != DRIVER or build['driver']['sha256'] != DRIVER:
        raise ValueError('Exact BC3 driver build mismatch')
    resources = {}
    for kind, vk_format in [('bc3-unorm', 137), ('bc3-srgb', 138)]:
        pack_dir = ROOT / '.local/logs/native-bc3-real-pack-v1' / kind
        pack = json.loads((pack_dir / 'native-pack.json').read_text())
        if not pack['complete'] or pack['driver_binary_sha256'] != DRIVER or pack['producer_source_sha256'] != PRODUCER:
            raise ValueError('Real pack driver/producer mismatch')
        srcs = {
            'native-layouts.bin': pack_dir / 'native-layouts.bin',
            'native-resource.bin': ROOT / '.local/logs/native-bc3-gpu-probe-v1/actual' / kind / 'native-resource.bin',
            'resource-input.bin': ROOT / '.local/build/native-bc3-producer-v1/real-inputs-v2' / kind / 'resource-input.bin',
        }
        table = srcs['native-layouts.bin'].read_bytes()
        # Public parser remains the authority. These explicit input bindings
        # prevent silently pairing a valid table with another payload/source.
        if sha(srcs['native-layouts.bin']) != pack['table_sha256'] or sha(srcs['native-resource.bin']) != pack['entries'][0]['native_payload_sha256']:
            raise ValueError('Real table/payload mismatch')
        recipe = table[160 + 8 + 40:160 + 1040]
        if int.from_bytes(recipe[32:36], 'little') != vk_format:
            raise ValueError('Unexpected recipe VkFormat')
        if sha(srcs['resource-input.bin']) != recipe[160:192].hex() or recipe[288:320].hex() != PRODUCER:
            raise ValueError('Real input/producer does not match the selected recipe')
        dst = out / 'resources' / kind
        dst.mkdir(parents=True)
        files = {}
        for leaf, original in srcs.items():
            shutil.copy2(original, dst / leaf)
            files[leaf] = record(dst / leaf)
        resources[kind] = {'files': files, 'pack_sha256': pack['pack_content_sha256'],
                           'producer_sha256': PRODUCER, 'vk_format': vk_format,
                           'pack_manifest': record(pack_dir / 'native-pack.json')}
    old = ROOT / '.local/build/turnip-one-native-family-aa185-resource4-v2'
    old_inputs = json.loads((old / 'fixture-inputs.json').read_text())
    shaders = {}
    for name, row in old_inputs['shaders'].items():
        if Path(name).is_absolute() or '..' in Path(name).parts:
            raise ValueError('Shader path')
        original = old / 'vulkan-fixture' / name
        if sha(original) != row['sha256']:
            raise ValueError('Original public fixture shader changed')
        target = out / 'vulkan-fixture' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        shaders[name] = record(target)
    inputs = {'driver_sha256': DRIVER, 'resources': resources, 'shaders': shaders}
    (out / 'fixture-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    ledger = {str(f.relative_to(source)): sha(f) for f in sorted(source.rglob('*'))
              if f.is_file() and '.local' not in f.relative_to(source).parts}
    (out / 'source-ledger.json').write_text(json.dumps(ledger, indent=2) + '\n')
    ready = {'base_revision': revision, 'source': str(source), 'source_ledger': record(out / 'source-ledger.json'),
             'fixture_inputs': record(out / 'fixture-inputs.json'), 'driver_build': record(driver_build),
             'driver': record(Path(build['driver']['path'])), 'script': record(Path(__file__)),
             'package': PACKAGE, 'activity': 'com.d2rmobile.nativeport.MainActivity',
             'device_executed': False, 'coherent_qualification': 'VERIFICATION_PENDING',
             'resident_lazy_requested': resident_lazy,
             'scope': 'Original public NVN 256x256/9mip full/native + original tail/B2T. Current exact-provider policy is preserved; unknown SHA retains FOREIGN. Explicit resident/lazy mode requires real owner2/lazy-active assertions; no game modules or performance claim.'}
    (out / 'source-ready.json').write_text(json.dumps(ready, indent=2) + '\n')
    return ready


def build(out, ready, jobs):
    source = Path(ready['source'])
    ledger = json.loads((out / 'source-ledger.json').read_text())
    if any(sha(source / n) != digest for n, digest in ledger.items()):
        raise ValueError('Frozen source changed')
    command = ['cmake', '-S', str(source / 'dm/host/android'), '-B', str(source / '.local/build/android-native'),
               '-G', 'Ninja', '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / '.local/android-sdk/ndk/28.2.13676358/build/cmake/android.toolchain.cmake'),
               '-DDM_ORIGINAL_ROMFS=' + str(ROOT / 'work/data/switch/3.3.94012/Update/RomFS'),
               '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-29']
    environment = dict(os.environ, DM_ROMFS_SRC=str(ROOT / 'work/data/switch/3.3.94012/Update/RomFS'))
    subprocess.run(command, check=True, env=environment)
    subprocess.run([sys.executable, str(source / 'scripts/dev/build-android-apk.py'), '--tlsdesc',
                    '--shared-pools', '--shared-import-windows', '--native-resources', '--native-jobs', str(jobs)], check=True, env=environment)
    apk = source / '.local/build/android-apk/d2r-native-tlsdesc-debug.apk'
    m = json.loads(apk.with_suffix('.apk.build.json').read_text())
    if sha(apk) != m['apk_sha256'] or len(m['native_library_sha256']) != 6:
        raise ValueError('Built APK identity/6-library contract')
    with zipfile.ZipFile(apk) as z:
        for name, digest in m['native_library_sha256'].items():
            if hashlib.sha256(z.read('lib/arm64-v8a/' + name)).hexdigest() != digest:
                raise ValueError('APK ELF mismatch: ' + name)
    final_ledger = {str(f.relative_to(source)): sha(f) for f in sorted(source.rglob('*'))
                    if f.is_file() and '.local' not in f.relative_to(source).parts}
    changed = [name for name, digest in ledger.items() if final_ledger.get(name) != digest]
    if any(name != 'dm/ios/Gen/assets/.stamp' for name in changed):
        raise ValueError('Frozen source changed while building: ' + repr(changed))
    (out / 'final-source-ledger.json').write_text(json.dumps(final_ledger, indent=2) + '\n')
    canonical = out / 'canonical'
    canonical.mkdir()
    shutil.copy2(apk, canonical / 'candidate.apk')
    m.update(package=PACKAGE, activity=ready['activity'], source_revision=ready['base_revision'],
             source_identity_authority='source-ready.json and source-ledger.json; no ambient parent HEAD authority',
             fixture_inputs_sha256=ready['fixture_inputs']['sha256'], driver_sha256=DRIVER,
             owned_resource_inputs={kind + '/' + n: row['sha256'] for kind, resource in json.loads((out / 'fixture-inputs.json').read_text())['resources'].items() for n, row in resource['files'].items()},
             shader_files={n: row['sha256'] for n, row in json.loads((out / 'fixture-inputs.json').read_text())['shaders'].items()},
             ordinary_UID_executed=False, new_original_NVNGPU_executed=False, default_enabled=False,
             resident_lazy_requested=ready.get('resident_lazy_requested', False),
             performance_baseline_eligible=False, coherent_qualification='VERIFICATION_PENDING',
             final_source_ledger=record(out / 'final-source-ledger.json'))
    (canonical / 'candidate.apk.build.json').write_text(json.dumps(m, indent=2) + '\n')
    result = {**ready, 'status': 'HOST_BUILT_AWAITING_ORIGINAL_NVN_GPU',
              'APK': record(canonical / 'candidate.apk'), 'native_libraries': m['native_library_sha256'],
              'candidate_manifest': record(canonical / 'candidate.apk.build.json'),
              'final_source_ledger': record(out / 'final-source-ledger.json'),
              'generated_stamp_changed': changed,
              'artifact_root': str(out), 'build_commands': [command, ['scripts/dev/build-android-apk.py', '--tlsdesc', '--shared-pools', '--shared-import-windows', '--native-resources']]}
    (canonical / 'ready.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--native-jobs', type=int, choices=range(1, 17), default=4)
    parser.add_argument('--resident-lazy', action='store_true',
                        help='isolated cold test: require actual exact-provider resident owner2 and lazy64K windows; never override provider admission')
    args = parser.parse_args()
    out = args.output.resolve()
    ready = prepare(out, args.resident_lazy)
    if args.build:
        ready = build(out, ready, args.native_jobs)
    print(json.dumps(ready, indent=2))


if __name__ == '__main__':
    main()
