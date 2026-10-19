#!/usr/bin/env bash
# 原 PNG 冻结指纹使用 zlib-ng；仅为资源生成子进程设置兼容库，不修改系统库。
# Frozen original PNG hashes use zlib-ng. Configure only the asset-generation
# subprocess, leaving system libraries unchanged. Original checks stay intact.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cd "$D2R_REPO_ROOT"
if [[ $# -gt 1 || ( $# == 1 && $1 != --overlay ) ]]; then
  echo 'usage: build-original-assets.sh [--overlay]' >&2; exit 2
fi
archive="$D2R_REPO_ROOT/.local/downloads/zlib-ng-2.3.3.tar.gz"
source_dir="$D2R_REPO_ROOT/.local/tools/zlib-ng-2.3.3"
expected=f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1
mkdir -p "$(dirname "$archive")" "$source_dir" .local/logs
if [[ ! -f "$archive" ]]; then
  curl -fL --retry 2 https://codeload.github.com/zlib-ng/zlib-ng/tar.gz/refs/tags/2.3.3 -o "$archive.tmp"
  mv "$archive.tmp" "$archive"
fi
printf '%s  %s\n' "$expected" "$archive" | sha256sum --check --status
if [[ ! -f "$source_dir/CMakeLists.txt" ]]; then
  tar -xzf "$archive" --strip-components=1 -C "$source_dir"
fi
cmake -S "$source_dir" -B "$source_dir/build" -DZLIB_COMPAT=ON \
  -DZLIB_ENABLE_TESTS=OFF -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release \
  > .local/logs/native-art-zlibng-build.log 2>&1
cmake --build "$source_dir/build" -j4 >> .local/logs/native-art-zlibng-build.log 2>&1
export LD_LIBRARY_PATH="$source_dir/build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
romfs="${DM_ROMFS_SRC:-$DM_ROOT/data/switch/3.3.94012/Update/RomFS}"
python3 dm/ios/tools/gen_assets.py "$romfs"
python3 dm/ios/tests/test_native_art_capture.py
if [[ ${1:-} == --overlay ]]; then
  python3 dm/ios/tools/gen_romfs_overlay.py "$romfs" .local/build/android-romfs-overlay
fi
