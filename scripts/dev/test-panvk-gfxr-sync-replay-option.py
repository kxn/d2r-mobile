#!/usr/bin/env python3
"""Check the sole runtime argument change against frozen baseline; no device."""
import ast,subprocess
from pathlib import Path
p=Path(__file__).with_name('run-panvk-gfxr-replay-fixture.py')
base=subprocess.check_output(['git','show','6758620:scripts/dev/run-panvk-gfxr-replay-fixture.py'],cwd=p.parent,text=True)
current=p.read_text()
expected=base.replace("'memory_translation':'none','validation'", "'memory_translation':'none','official_sync_queue_submissions':True,'single_changed_replay_option':'--sync','diagnostic_serialization_not_runtime_fix':True,'validation'").replace("args='--validate --log-file ","args='--sync --validate --log-file ")
assert current==expected, 'Unexpected mutation beyond plan and official --sync argument'
assert current.count("args='--sync --validate --log-file ")==1
ast.parse(current)
print('PASS: exact baseline controller; only official --sync argv and plan declaration differ; all lifecycle/identity/input/cleanup checks retained')
