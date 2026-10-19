#!/usr/bin/env python3
"""Build an unmodified, pinned Mesa Turnip driver for app-local Android tests.

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

ROOT = Path(__file__).resolve().parents[2]
VERSION = '26.2.5'
SOURCE_SHA = '794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5'
URL = f'https://archive.mesa3d.org/mesa-{VERSION}.tar.xz'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work-dir', type=Path, default=ROOT / '.local/build/turnip')
parser.add_argument('--ndk', type=Path, default=os.environ.get('ANDROID_NDK_HOME'))
parser.add_argument('--meson', default='meson')
parser.add_argument('--source-archive', type=Path, help='reuse a downloaded release archive; checksum is still required')
parser.add_argument('--build-tools-bin', type=Path, help='optional unpacked Bison/Flex/M4 bin directory')
parser.add_argument('-j', type=int, default=8)
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
# Reject modifications to extracted source; retain the pinned archive as input.
# Also reject extra source files instead of silently compiling local patches.
with tarfile.open(archive) as tf:
    expected = set()
    for member in tf:
        if not member.isfile():
            continue
        local = work / member.name
        expected.add(local.resolve())
        with tf.extractfile(member) as f:
            digest = hashlib.file_digest(f, 'sha256').hexdigest()
        if not local.is_file() or sha(local) != digest:
            raise SystemExit(f'extracted source differs from archive: {local}')
extra = {p.resolve() for p in source.rglob('*') if p.is_file()
         and not (p.suffix == '.pyc' and '__pycache__' in p.parts)} - expected
if extra:
    raise SystemExit(f'extra files in source tree: {sorted(extra)[:5]}')

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
               'extracted_source_verified': True, 'patches': []},
    'builder_sha256': sha(Path(__file__).resolve()),
    'cross_file_sha256': sha(cross), 'meson_flags': flags,
    'compiler_version': run(compiler, '--version', capture=True).stdout,
    'ndk_source_properties': (ndk / 'source.properties').read_text(),
    'tool_versions': {name: run(tool, '--version', capture=True).stdout.splitlines()[0]
                      for name, tool in [('meson', args.meson), ('ninja', 'ninja'),
                                         ('bison', 'bison'), ('flex', 'flex'), ('m4', 'm4')]},
    'driver_sha256': sha(driver), 'driver_bytes': driver.stat().st_size,
    'installed': False,
}
(output / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(output / driver.name)
