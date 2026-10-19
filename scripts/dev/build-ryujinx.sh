#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/env.sh"
cd "$D2R_REPO_ROOT/.local/src/ryujinx"
expected=793ca017ff4f6e48b798a62d2486405bfc42ef39
test "$(git rev-parse HEAD)" = "$expected" || { echo 'Unexpected Ryujinx source revision'; exit 1; }
patch="$D2R_REPO_ROOT/scripts/dev/ryujinx-controls.patch"
if ! git apply --reverse --check "$patch" 2>/dev/null; then
    git apply --check "$patch"
    git apply "$patch"
fi
dotnet publish src/Ryujinx/Ryujinx.csproj -c Release -r linux-x64 \
    --self-contained true --configfile "$D2R_REPO_ROOT/scripts/dev/nuget.config" \
    -o "$D2R_REPO_ROOT/.local/emulators/ryujinx" \
    -p:DebugType=embedded -p:ExtraDefineConstants=DISABLE_UPDATER \
    -p:PublishTrimmed=false -p:RuntimeIdentifiers=linux-x64 \
    -p:Version=0.0.0-d2r-env.793ca017 -v minimal
