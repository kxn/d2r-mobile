#!/usr/bin/env bash
# 同一套原 NVN / Vulkan 语义检查在 ARM64 真机运行；不代表真实游戏画面验收。
# Run original NVN/Vulkan semantic checks on ARM64 hardware; this is not real-game visual acceptance.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cd "$D2R_REPO_ROOT"
serial=${1:-JNJF49UGBQ4XSCZT}
if [[ $# -gt 1 ]]; then echo 'usage: test-android-vulkan.sh [serial]' >&2; exit 2; fi
build="$D2R_REPO_ROOT/.local/build/android-native"
fixture="$D2R_REPO_ROOT/.local/build/vulkan-fixture"
python dm/host/android/tests/make_vulkan_fixture.py "$fixture"
cmake --build "$build" -j4
adb -s "$serial" shell 'mkdir -p /data/local/tmp/d2r-native/vulkan-fixture'
adb -s "$serial" push "$build/native_vulkan_test" /data/local/tmp/d2r-native/native_vulkan_test
adb -s "$serial" push --sync "$fixture/." /data/local/tmp/d2r-native/vulkan-fixture
mkdir -p .local/logs
adb -s "$serial" shell 'cd /data/local/tmp/d2r-native && chmod 755 native_vulkan_test && DM_VULKAN_SHADERS=/data/local/tmp/d2r-native/vulkan-fixture timeout 30 ./native_vulkan_test' > .local/logs/android-vulkan-test.log 2>&1
cat .local/logs/android-vulkan-test.log
python - <<'PY'
from pathlib import Path
import json
lines=Path('.local/logs/android-vulkan-test.log').read_text().splitlines()
results=[json.loads(line) for line in lines if line.startswith('{')]
assert results and results[-1]['result']=='passed'
assert not any('【红】' in line or 'FATAL' in line for line in lines)
Path('.local/logs/android-vulkan-result.json').write_text(json.dumps({**results[-1],'full_game_playable':False,'real_game_rendering':'real Android Act I camp, save/quit/relaunch and touch movement reached; full gameplay acceptance pending'},indent=2)+'\n')
PY
