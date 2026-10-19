#!/usr/bin/env python3
"""Build pinned Mesa plus the declared experimental standard host-import patch, offline only.

Requires an Android NDK, Meson, Ninja, Mako, PyYAML, packaging, Bison, Flex
and M4. This does not install a driver or change Android's system drivers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

PACKAGE = Path(__file__).resolve().parent
ROOT = Path(__file__).resolve().parents[3]
PATCH = PACKAGE / '0001-kgsl-coherent-host-import-ext.patch'
BUILDER_SHA = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
VERSION = '26.2.5'
SOURCE_SHA = '794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5'
URL = f'https://archive.mesa3d.org/mesa-{VERSION}.tar.xz'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work-dir', type=Path, default=ROOT / '.local/build/turnip-host-import-ext-v2')
parser.add_argument('--ndk', type=Path, default=os.environ.get('ANDROID_NDK_HOME'))
parser.add_argument('--meson', default='meson')
parser.add_argument('--source-archive', type=Path, help='reuse a downloaded release archive; checksum is still required')
parser.add_argument('--build-tools-bin', type=Path, help='optional unpacked Bison/Flex/M4 bin directory')
parser.add_argument('-j', type=int, default=2)
args = parser.parse_args()
if args.ndk is None or args.j < 1:
    parser.error('--ndk (or ANDROID_NDK_HOME) and positive -j are required')
work = args.work_dir.resolve()
work.mkdir(parents=True, exist_ok=True)
ndk = args.ndk.resolve()
toolchain = ndk / 'toolchains/llvm/prebuilt/linux-x86_64'
compiler = toolchain / 'bin/aarch64-linux-android34-clang'
if not compiler.is_file():
    parser.error('expected Linux x86_64 NDK toolchain with Android API 34')
env = os.environ.copy()
env['PYTHONDONTWRITEBYTECODE'] = '1'
# Mesa otherwise embeds the incidental source-checkout HEAD in its binary.
env['MESA_GIT_SHA1_OVERRIDE'] = hashlib.sha256(PATCH.read_bytes()).hexdigest()[:10]
meson_program = shutil.which(args.meson)
if meson_program is None:
    parser.error('Meson was not found')
# Mesa's generators discover Python through PATH independently of Meson's
# shebang. Keep both in the requested build environment.
env['PATH'] = str(Path(meson_program).resolve().parent) + os.pathsep + env['PATH']
if args.build_tools_bin:
    tools = args.build_tools_bin.resolve()
    env['PATH'] = str(tools) + os.pathsep + env['PATH']
    env['BISON_PKGDATADIR'] = str(tools.parent / 'share/bison')
    env['M4'] = str(tools / 'm4')


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def run(*cmd, capture=False):
    return subprocess.run([str(x) for x in cmd], env=env, check=True,
                          text=True, capture_output=capture)


archive = work / f'mesa-{VERSION}.tar.xz'
if not archive.exists():
    partial = archive.with_suffix('.partial')
    if args.source_archive:
        shutil.copy2(args.source_archive, partial)
    else:
        urllib.request.urlretrieve(URL, partial)
    if sha(partial) != SOURCE_SHA:
        raise SystemExit('upstream source checksum mismatch')
    partial.replace(archive)
if sha(archive) != SOURCE_SHA:
    raise SystemExit('cached source checksum mismatch')
source = work / f'mesa-{VERSION}'
if not source.exists():
    with tarfile.open(archive) as tf:
        tf.extractall(work, filter='data')
# Materialize expected patched bytes from the verified upstream archive in a
# temporary tree. This validates both cache hits and fresh builds; no mutation
# of any upstream build tree outside --work-dir is permitted.
import tempfile
with tempfile.TemporaryDirectory(prefix='turnip-host-import-verify-') as temp:
    temp_root = Path(temp)
    with tarfile.open(archive) as tf:
        tf.extractall(temp_root, filter='data')
    expected_source = temp_root / f'mesa-{VERSION}'
    run('git', '-C', expected_source, 'apply', '--check', PATCH)
    run('git', '-C', expected_source, 'apply', PATCH)
    expected = {}
    for p in expected_source.rglob('*'):
        if p.is_file() or p.is_symlink():
            expected[p.relative_to(expected_source).as_posix()] = (
                {'kind': 'symlink', 'target': os.readlink(p)} if p.is_symlink()
                else {'kind': 'file', 'sha256': sha(p)})
    # The first run may point to a newly extracted, still unpatched tree.
    changed = subprocess.run(['git', '-C', str(source), 'apply', '--check', str(PATCH)],
                             env=env, text=True, capture_output=True)
    if changed.returncode == 0:
        run('git', '-C', source, 'apply', PATCH)
    actual = {p.relative_to(source).as_posix(): (
                  {'kind': 'symlink', 'target': os.readlink(p)} if p.is_symlink()
                  else {'kind': 'file', 'sha256': sha(p)})
              for p in source.rglob('*')
              if (p.is_file() or p.is_symlink()) and '.git' not in p.relative_to(source).parts
              and not (p.suffix == '.pyc' and '__pycache__' in p.parts)}
    if actual != expected:
        differences = sorted(k for k in set(actual) | set(expected)
                             if actual.get(k) != expected.get(k))
        raise SystemExit(f'Patched source differs from declared inputs: {differences[:10]}')
patched_tree_sha = hashlib.sha256(json.dumps(expected, sort_keys=True).encode()).hexdigest()

pc = work / 'target-pkgconfig'
pc.mkdir(exist_ok=True)
zlib_header = (toolchain / 'sysroot/usr/include/zlib.h').read_text()
import re
zlib_version = re.search(r'#define ZLIB_VERSION\s+"([^"]+)"', zlib_header).group(1)
(pc / 'zlib.pc').write_text(
    f'Name: zlib\nDescription: Android NDK target stubs\nVersion: {zlib_version}\nLibs: -lz\nCflags:\n')
cross = work / 'android-aarch64.ini'
cross.write_text(f'''[binaries]
c = '{compiler}'
cpp = '{toolchain / 'bin/aarch64-linux-android34-clang++'}'
ar = '{toolchain / 'bin/llvm-ar'}'
strip = '{toolchain / 'bin/llvm-strip'}'
pkg-config = 'pkg-config'
[properties]
needs_exe_wrapper = true
pkg_config_libdir = ['{pc}']
sys_root = '{toolchain / 'sysroot'}'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
[built-in options]
c_args = ['-fPIC']
cpp_args = ['-fPIC', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables']
c_link_args = ['-Wl,-z,max-page-size=16384']
cpp_link_args = ['-static-libstdc++', '-Wl,-z,max-page-size=16384']
[cmake]
CMAKE_FIND_ROOT_PATH = ['{toolchain / 'sysroot'}']
CMAKE_FIND_ROOT_PATH_MODE_PACKAGE = 'ONLY'
''')
build = work / 'mesa-kgsl-build'
flags = ['--wrap-mode=nofallback', '-Dbuildtype=release', '-Dplatforms=android',
         '-Dplatform-sdk-version=34', '-Dandroid-stub=true',
         '-Dandroid-libbacktrace=disabled', '-Degl=disabled', '-Dgallium-drivers=',
         '-Dvulkan-drivers=freedreno', '-Dfreedreno-kmds=kgsl',
         '-Dzstd=disabled', '-Dzlib=enabled', '-Dllvm=disabled',
         '-Dspirv-tools=disabled', '-Dxmlconfig=disabled', '-Dexpat=disabled',
         '-Dpkg_config_path=' + str(pc)]
setup = [args.meson, 'setup']
if (build / 'build.ninja').exists():
    setup += ['--reconfigure', '--clearcache']
run(*setup, build, source, '--cross-file', cross, *flags)
cached_manifest = work / 'driver/build.json'
cached_driver = build / 'src/freedreno/vulkan/libvulkan_freedreno.so'
if cached_manifest.is_file() and cached_driver.is_file():
    if sha(cached_driver) != json.loads(cached_manifest.read_text())['driver_sha256']:
        raise SystemExit('Cached driver differs from its recorded output hash')
run(args.meson, 'compile', '-C', build, '-j', args.j)
driver = build / 'src/freedreno/vulkan/libvulkan_freedreno.so'
elf = run(toolchain / 'bin/llvm-readelf', '-h', '-d', '-l', driver, capture=True).stdout
if 'AArch64' not in elf or 'x86_64' in elf:
    raise SystemExit('driver architecture or dynamic dependencies are invalid')
output = work / 'driver'
output.mkdir(exist_ok=True)
shutil.copy2(driver, output / driver.name)
(output / 'elf.txt').write_text(elf)
manifest = {
    'source': {'url': URL, 'version': VERSION, 'sha256': SOURCE_SHA,
               'extracted_source_verified': True, 'patches': [{'path': PATCH.name, 'sha256': sha(PATCH)}],
               'patched_tree_sha256': patched_tree_sha},
    'builder_sha256': BUILDER_SHA,
    'mesa_git_sha1_override': env['MESA_GIT_SHA1_OVERRIDE'],
    'cross_file_sha256': sha(cross), 'meson_flags': flags,
    'compiler_version': run(compiler, '--version', capture=True).stdout,
    'ndk_source_properties': (ndk / 'source.properties').read_text(),
    'tool_versions': {name: run(tool, '--version', capture=True).stdout.splitlines()[0]
                      for name, tool in [('meson', args.meson), ('ninja', 'ninja'),
                                         ('bison', 'bison'), ('flex', 'flex'), ('m4', 'm4')]},
    'driver_sha256': sha(driver), 'driver_bytes': driver.stat().st_size,
    'installed': False,
    'experimental_api_default_off': True,
    'activation_environment': 'TU_DM_HOST_IMPORT=true',
    'extension_advertised': 'only after opt-in and actual coherent useraddr import/INFO/release probe',
    'runtime_kernel_support_verified': False,
}
(output / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(output / driver.name)
