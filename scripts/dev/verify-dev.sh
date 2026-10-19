#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/env.sh"
bash "$D2R_REPO_ROOT/scripts/dev/check-env.sh"
python "$D2R_REPO_ROOT/scripts/dev/test_oracle_tools.py"
python "$D2R_REPO_ROOT/scripts/dev/smoke-gdb.py"
python "$D2R_REPO_ROOT/scripts/dev/smoke-oracle.py"
bash "$D2R_REPO_ROOT/scripts/dev/run-device-probe.sh"
