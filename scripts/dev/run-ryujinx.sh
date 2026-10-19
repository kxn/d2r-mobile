#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/env.sh"
profile="$DM_ROOT/ryujinx"
mkdir -p "$profile" "$DM_ROOT/keys"
if test -s "$D2R_REPO_ROOT/prod.keys" && ! test -e "$DM_ROOT/keys/prod.keys" && ! test -L "$DM_ROOT/keys/prod.keys"; then
    ln -s "$D2R_REPO_ROOT/prod.keys" "$DM_ROOT/keys/prod.keys"
fi
if ! test -e "$profile/system" && ! test -L "$profile/system"; then
    ln -s "$DM_ROOT/keys" "$profile/system"
fi
args=("$D2R_REPO_ROOT/.local/emulators/ryujinx/Ryujinx")
if test "${D2R_NO_GUI:-0}" = 1; then
    args+=(--no-gui --root-data-dir "$profile" --enable-gdb-stub --gdb-stub-port 55571)
else
    args+=(--root-data-dir "$profile")
fi
if test -z "${DISPLAY:-}" && test -z "${WAYLAND_DISPLAY:-}"; then
    exec xvfb-run -a -s '-screen 0 1280x720x24' "${args[@]}" "$@"
fi
exec "${args[@]}" "$@"
