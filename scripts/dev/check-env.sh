#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/env.sh"
failed=0
for tool in git clang cmake ninja dotnet python adb javac keytool glslangValidator spirv-val spirv-cross vulkaninfo hactool; do
    if command -v "$tool" >/dev/null; then
        printf 'OK   %-18s %s\n' "$tool" "$(command -v "$tool")"
    else
        printf 'FAIL %s\n' "$tool"
        failed=1
    fi
done
python -c 'import PIL, numpy, lz4.block, capstone, unicorn; print("OK   Python imports")' || failed=1
ndk_cc="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang"
if test -x "$ndk_cc"; then printf 'OK   Android ARM64 compiler\n'; else printf 'FAIL Android ARM64 compiler\n'; failed=1; fi
translator="$D2R_REPO_ROOT/.local/build/dotnet/bin/dmsh_xlate/release/dmsh_xlate.dll"
if test -f "$translator"; then dotnet "$translator" --source-sha; else printf 'FAIL shader translator not built\n'; failed=1; fi
if test -x "$D2R_REPO_ROOT/.local/emulators/ryujinx/Ryujinx"; then
    printf 'OK   Ryujinx source build (GDB-enabled, not the author\x27s private build)\n'
else
    printf 'FAIL Ryujinx source build\n'; failed=1
fi
printf '\nAndroid devices:\n'
adb devices -l
printf '\nGame inputs (not supplied by this repository):\n'
for item in data/switch/3.3.94012/Update/ExeFS/main data/switch/3.3.94012/Update/ExeFS/sdk data/switch/3.3.94012/Update/ExeFS/subsdk0 data/switch/3.3.94012/Update/RomFS/data/build_manifest.psv keys/prod.keys; do
    if test -s "$DM_ROOT/$item"; then
        printf 'FOUND   %s\n' "$DM_ROOT/$item"
    elif test "$item" = keys/prod.keys && test -s "$D2R_REPO_ROOT/prod.keys"; then
        printf 'FOUND   %s (launcher links into the isolated profile)\n' "$D2R_REPO_ROOT/prod.keys"
    else
        printf 'MISSING %s\n' "$DM_ROOT/$item"
    fi
done
printf 'Firmware: install your console dump into the isolated Ryujinx profile.\n'
printf '\nTool checks only; missing game inputs are not a game-test pass.\n'
exit "$failed"
