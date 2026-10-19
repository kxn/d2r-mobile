#!/usr/bin/env python3
# 等待系统编辑框真实出现，再输入、核对并提交；仅操作本项目的对话框。
# Wait for the actual system editor, enter and verify text, then submit only
# this project's dialog.
import argparse
import json
import re
import subprocess
import time
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('text')
parser.add_argument('--timeout', type=float, default=45)
parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
args = parser.parse_args()
if not re.fullmatch(r'[A-Za-z][A-Za-z_-]{0,13}[A-Za-z]', args.text):
    parser.error('use a 2–15 character ASCII test name')
base = ['adb', '-s', args.serial]
package = 'com.d2rmobile.nativeport'
path = '/data/local/tmp/d2r-game-editor.xml'
def adb(*parts):
    return subprocess.check_output([*base, *parts], text=True, stderr=subprocess.STDOUT)
def hierarchy():
    adb('shell', 'uiautomator', 'dump', path)
    return ET.fromstring(adb('shell', 'cat', path))
def own(root, kind):
    return [n for n in root.iter('node') if n.get('package') == package and n.get('class') == kind]
deadline = time.monotonic() + args.timeout
while time.monotonic() < deadline:
    if own(hierarchy(), 'android.widget.EditText'):
        break
else:
    raise SystemExit('game keyboard dialog did not appear')
adb('shell', 'input', 'keycombination', '113', '29')
adb('shell', 'input', 'text', args.text)
# 拼音输入法可能仍持有组合串；先确认组合串，再隐藏键盘。
# A Pinyin IME may retain a composing string; commit it before hiding the IME.
adb('shell', 'input', 'keyevent', 'KEYCODE_ENTER')
adb('shell', 'input', 'keyevent', 'KEYCODE_BACK')
root = hierarchy()
editors = own(root, 'android.widget.EditText')
if len(editors) != 1 or editors[0].get('text') != args.text:
    raise SystemExit('Android editor content differs from requested name')
buttons = [n for n in own(root, 'android.widget.Button') if n.get('resource-id') == 'android:id/button1']
if len(buttons) != 1:
    raise SystemExit('game keyboard confirm button unavailable')
x0, y0, x1, y1 = map(int, re.findall(r'\d+', buttons[0].get('bounds')))
def sample():
    lines = adb('exec-out', 'run-as', package, 'tail', '-n', '3', 'files/runtime/hid.jsonl').splitlines()
    return next((json.loads(line) for line in reversed(lines) if 'npad_return' in line), None)
before = sample()
adb('shell', 'input', 'tap', str((x0+x1)//2), str((y0+y1)//2))
# 原游戏在键盘返回后的 UI 更新才消费名字；确认至少三次新采样再允许下一步操作。
# The original game consumes the name in its subsequent UI update. Wait for
# three new input samples before allowing the next flow action.
deadline = time.monotonic() + args.timeout
while time.monotonic() < deadline:
    current = sample()
    if before and current and current['sampling'] >= before['sampling'] + 3 and current['buttons'] == 0:
        break
    time.sleep(0.3)
else:
    raise SystemExit('keyboard submitted, but game input polling has not resumed')
print('Verified Android editor and submitted: ' + args.text)
