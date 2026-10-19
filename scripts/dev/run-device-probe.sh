#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/env.sh"
exec "$D2R_REPO_ROOT/.local/build/device-probe/device-probe-linux"
