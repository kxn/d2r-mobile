#!/usr/bin/env python3
"""Use the repository's unmodified GDB client against our synthetic NRO."""
from pathlib import Path
import argparse
import json
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'dm/verify/oracle'))
from swgdb import Gdb

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, default=55571)
args = parser.parse_args()
g = Gdb(port=args.port, timeout=10)
bp = None
try:
    stop = g.interrupt()
    g.select(stop)
    modules, info = g.modules()
    pc = g.reg(32)
    # The probe starts suspended at its page-aligned entry point.
    base = pc & ~0xfff
    assert g.read(base, 4) == bytes.fromhex('20000014'), (hex(pc), info)
    assert int.from_bytes(g.read(base + 0x2008, 8), 'little') == 0x44325250524f4245
    marker = (0x1122334455667788).to_bytes(8, 'little')
    assert g.write(base + 0x2008, marker)
    assert g.read(base + 0x2008, 8) == marker
    bp = base + 0x88
    assert g.set_bp(bp)
    assert g.cont() == b'OK'
    stop = g.wait_stop(timeout=15)
    assert g.reg(32) == bp
    assert g.reg(19) == base + 0x2000
    assert g.step_over(bp, stop)
    assert g.reg(20) == 1
    assert g.cont() == b'OK'
    g.wait_stop(timeout=15)
    assert g.reg(32) == bp
    assert int.from_bytes(g.read(base + 0x2000, 8), 'little') == 1
    print(json.dumps({'result': 'PASS', 'scope': 'synthetic NRO, not D2R validation',
                      'checks': ['interrupt', 'registers', 'memory read/write', 'breakpoint',
                                 'continue', 'step-over', 'guest execution'],
                      'module_query': 'responded; D2R module naming not verified'}, indent=2))
finally:
    if bp is not None:
        g.clear_bp(bp)
    g.close()
