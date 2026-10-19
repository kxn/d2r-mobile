#!/usr/bin/env bash
# 在已授权的真机运行原宿主与原测试；图形边界是显式未完成状态。
# Run the original host and tests on an authorized device; the graphics boundary is explicitly incomplete.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cd "$D2R_REPO_ROOT"
if [[ $# -gt 1 ]]; then echo 'usage: test-android-native.sh [device-serial]' >&2; exit 2; fi
adb_args=()
if [[ $# == 1 ]]; then adb_args=(-s "$1"); fi
adb "${adb_args[@]}" get-state >/dev/null
build="$D2R_REPO_ROOT/.local/build/android-native"
fixture="$D2R_REPO_ROOT/.local/build/native-fixture"
rewrite="$D2R_REPO_ROOT/.local/build/native-rewrite"
remote=/data/local/tmp/d2r-native
logs="$D2R_REPO_ROOT/.local/logs"
mkdir -p "$logs"
python scripts/dev/rewrite-native.py --source "${DM_NSO_DIR:-$DM_ROOT/data/switch/3.3.94012/Update/ExeFS}" \
  --output "$rewrite" --abi elf --rules native --verify-only
for file in dmhost_native native_rule_test native_fs_test native_hid_test native_touch_test; do
  [[ -x "$build/$file" ]] || { echo 'Run build-android-native.sh --rewrite first' >&2; exit 1; }
done
adb "${adb_args[@]}" shell "mkdir -p $remote/modules $remote/fixture $remote/temp $remote/save"
adb "${adb_args[@]}" push "$build/dmhost_native" "$build/native_rule_test" "$build/native_fs_test" "$build/native_hid_test" "$build/native_touch_test" "$remote/"
adb "${adb_args[@]}" shell "chmod 700 $remote/dmhost_native $remote/native_rule_test $remote/native_fs_test $remote/native_hid_test $remote/native_touch_test"
for mod in main sdk subsdk0; do
  adb "${adb_args[@]}" push "$rewrite/$mod.patched.bin" "$rewrite/$mod.fixups.tsv" "$rewrite/$mod.gates.tsv" "$remote/modules/"
done
adb "${adb_args[@]}" push "$fixture/fixture.patched.bin" "$fixture/fixture.fixups.tsv" "$fixture/fixture.gates.tsv" "$remote/fixture/"
adb "${adb_args[@]}" push --sync "$DM_ROOT/data/switch/3.3.94012/Update/RomFS" "$remote/" > "$logs/android-native-resource-copy.log" 2>&1
adb "${adb_args[@]}" shell "timeout 40 $remote/native_rule_test $remote/fixture" > "$logs/android-native-rule-test.log" 2>&1
adb "${adb_args[@]}" shell "TMPDIR=$remote/temp timeout 40 $remote/native_fs_test" > "$logs/android-native-fs-test.log" 2>&1
adb "${adb_args[@]}" shell "timeout 40 $remote/native_hid_test && timeout 40 $remote/native_touch_test" > "$logs/android-native-input-test.log" 2>&1
config="DM_ROOT=$remote DM_ROMFS=$remote/RomFS DM_SAVE=$remote/save DM_TEMP=$remote/temp DM_OPMODE=0"
adb "${adb_args[@]}" shell "$config timeout 40 $remote/dmhost_native $remote/modules --constructors" > "$logs/android-native-constructors.log" 2>&1
status=0
adb "${adb_args[@]}" shell "$config timeout 40 $remote/dmhost_native $remote/modules --run" > "$logs/android-native-nnmain.log" 2>&1 || status=$?
if [[ $status != 78 ]]; then
  tail -25 "$logs/android-native-nnmain.log" >&2
  echo "Expected the explicit graphics boundary (78), received $status" >&2; exit 1
fi
python - "$logs" <<'PY'
import json,sys,re
from pathlib import Path
logs=Path(sys.argv[1])
def report(name):
    lines=(logs/name).read_text().splitlines()
    return next(json.loads(s) for s in reversed(lines) if s.startswith('{'))
rules=report('android-native-rule-test.log')
assert rules['backend']=='native-arm64' and rules['result']=='passed'
assert rules['atomic_increments']==200000 and rules['signal_deliveries']>0
fs=(logs/'android-native-fs-test.log').read_text()
assert re.search(r'== 68 项，失败 0 ==',fs)
hid=report('android-native-input-test.log')
assert hid['result']=='passed' and hid['hid_checks']==15
assert '== 13 项，失败 0 ==' in (logs/'android-native-input-test.log').read_text()
constructors=report('android-native-constructors.log')
assert constructors['stage']=='constructors-completed'
boundary=report('android-native-nnmain.log')
assert boundary['stage']=='graphics-boundary' and boundary['graphics_available'] is False
result={'backend':'native-arm64','original_host':True,'cpu_emulator_used':False,
        'native_rules':rules,'original_fs_checks':68,'fs_failed':0,'headless_hid_checks':hid['hid_checks'],'original_touch_checks':13,
        'sdk_and_constructors':'completed','nnMain':'reached NVN graphics boundary',
        'graphics_available':False,'full_game_playable':False}
(logs/'android-native-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
PY
