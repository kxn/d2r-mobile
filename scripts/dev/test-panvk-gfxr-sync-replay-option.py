#!/usr/bin/env python3
"""Offline audit of actual official options and cold property transaction."""
import ast
from pathlib import Path
p=Path(__file__).with_name('run-panvk-gfxr-replay-fixture.py');s=p.read_text();t=ast.parse(s)
assert "--memory-translation rebind"in s and "--memory-translation none"not in s
assert "--sync --serialize-render-passes --validate"in s
assert '--isolate-render-passes'not in s and '--debug-device-lost'not in s
assert "'bc_compute'if a.bc_route=='compute'else'0'"in s
assert s.index("property_restore_required=True;save()")<s.index("write_debug_property(plan['debug_property_requested'])")<s.index("launch=subprocess.Popen")
final=next(n for n in t.body if isinstance(n,ast.Try)).finalbody
assert "debug property restore:"in ast.unparse(ast.Module(body=final,type_ignores=[]))
assert 'Initial PANVK_DEBUG environment overrides Android property'in s
print('PASS: fixed official rebind/sync/render-pass args; no forbidden options; route-only cold property and journal-before-write/finally-restore')
