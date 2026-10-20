#!/usr/bin/env python3
"""Verify only registered render-pass serialization was added to sync baseline."""
import ast,subprocess
from pathlib import Path
p=Path(__file__).with_name('run-panvk-gfxr-replay-fixture.py')
base=subprocess.check_output(['git','show','9c64b17:scripts/dev/run-panvk-gfxr-replay-fixture.py'],cwd=p.parent,text=True)
current=p.read_text()
expected=base.replace("'single_changed_replay_option':'--sync'","'single_changed_replay_option':'--serialize-render-passes','official_serialize_render_passes':True").replace("args='--sync --validate --log-file ","args='--sync --serialize-render-passes --validate --log-file ")
assert current==expected, 'Unexpected change beyond one registered option and plan declaration'
assert current.count("args='--sync --serialize-render-passes --validate --log-file ")==1
assert '--isolate-render-passes' not in current and '--debug-device-lost' not in current
ast.parse(current)
print('PASS: exact sync baseline; only --serialize-render-passes argv and plan declaration added; all identity/input/PID/cleanup guards retained')
