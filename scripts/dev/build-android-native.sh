#!/usr/bin/env bash
# 编译原项目原生宿主；--rewrite 生成/更新真实游戏改写产物。
# Build the original native host; --rewrite generates/updates real game rewrites.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
# CLI 边界沿用 native；APK 的完整原触屏规则用 --touch 单独生成，避免混缓存。
# Keep the CLI boundary on native rules. --touch generates the APK's complete
# original touch rules in a separate directory to avoid mixing ABI artifacts.
cd "$D2R_REPO_ROOT"
rewrite=0
shaders=0
touch=0
tlsdesc=0
for argument in "$@"; do
  case "$argument" in
    --rewrite) rewrite=1 ;;
    --shaders) shaders=1 ;;
    --touch) touch=1; rewrite=1 ;;
    --tlsdesc) tlsdesc=1 ;;
    *) echo 'usage: build-android-native.sh [--rewrite | --touch [--tlsdesc]] [--shaders]' >&2; exit 2 ;;
  esac
done
if [[ $tlsdesc == 1 && $touch == 0 ]]; then
  echo '--tlsdesc requires --touch (the APK path)' >&2; exit 2
fi
host_abi=elf
tls_option=OFF
if [[ $tlsdesc == 1 ]]; then host_abi=elfdesc; tls_option=ON; fi
if [[ $touch == 1 ]]; then
  export DM_RULESET=touch-run-v4
  scripts/dev/build-original-assets.sh --overlay
fi
if [[ $shaders == 1 ]]; then scripts/dev/build-vulkan-shaders.sh; fi
build="$D2R_REPO_ROOT/.local/build/android-native"
cmake -S dm/host/android -B "$build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DDM_ANDROID_TLS_DESC="$tls_option"
cmake --build "$build" -j4
fixtures="$D2R_REPO_ROOT/.local/build/native-fixture"
python dm/host/android/tests/make_fixture.py "$fixtures"
DM_HOST_ABI=elf python dm/tools/patch_nso.py "$fixtures/fixture.nso" "$fixtures" fixture
if [[ $rewrite == 1 ]]; then
  src=${DM_NSO_DIR:-$DM_ROOT/data/switch/3.3.94012/Update/ExeFS}
  out="$D2R_REPO_ROOT/.local/build/native-rewrite"
  if [[ $touch == 1 ]]; then out="$D2R_REPO_ROOT/.local/build/native-touch-rewrite"; fi
  if [[ $tlsdesc == 1 ]]; then out="$D2R_REPO_ROOT/.local/build/native-touch-tlsdesc-rewrite"; fi
  python scripts/dev/rewrite-native.py --source "$src" --output "$out" \
    --abi "$host_abi" --rules "${DM_RULESET:-native}"
fi
if llvm-readelf -d "$build/dmhost_native" | rg -qi 'unicorn|qemu|ryujinx'; then
  echo 'Forbidden CPU emulator dependency (AGENTS.md)' >&2; exit 1
fi
echo "Native ARM64 host: $build/dmhost_native"
