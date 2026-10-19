#!/usr/bin/env python3
"""Exercise patched Ryujinx, HID, capture timeout and real guest state extraction."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from emulator_session import ROOT, session
sys.path.insert(0, str(ROOT / 'dm/verify/oracle'))
sys.path.insert(0, str(ROOT / 'dm/verify'))
from control import command, set_input, screenshot
from record import record
from selfcmp import load_ticks, compare
from swgdb import Gdb


def run(nro, output, corrupt=False):
    with session(nro, output) as port:
        gdb = Gdb(port=port, timeout=15)
        try:
            stop = gdb.interrupt()
            gdb.select(stop)
            base = gdb.reg(32) & ~0xfff
            assert gdb.read(base, 4) == bytes.fromhex('20000014')
            assert command(gdb, 'info')['protocol'] == 1
            state = set_input(gdb, 1, 12000, -12000, -3000, 4000)
            assert state['buttons'] & 1 and (state['lx'], state['ly'], state['rx'], state['ry']) == (12000, -12000, -3000, 4000), state
            neutral = set_input(gdb)
            assert neutral['buttons'] == 0 and neutral['lx'] == 0 and neutral['sample'] > state['sample'], neutral
            try:
                command(gdb, 'input 1 32768 0 0 0')
            except RuntimeError:
                pass
            else:
                raise AssertionError('Invalid stick input was accepted')
            # The fixture never renders: capture must time out, never create a blank "success".
            try:
                screenshot(gdb, output / 'should-not-exist.png', timeout=0.2)
            except TimeoutError:
                pass
            else:
                raise AssertionError('Non-rendering fixture unexpectedly produced a screenshot')
            assert not (output / 'should-not-exist.png').exists()
            assert command(gdb, 'shot-status')['status'] == 'idle'
            command(gdb, 'release')
            assert not command(gdb, 'input-state')['active']

            def fault(client, tick, game, units):
                assert len(units) == 5
                if corrupt and tick == 5:
                    player = next(u for u in units if u['type'] == 0)['ptr']
                    assert client.write(player + 0x158, player.to_bytes(8, 'little'))

            try:
                record(gdb, base + 0x300, 25, output / 'capture', on_tick=fault)
            except ValueError as error:
                if not corrupt or 'Invalid unit list' not in str(error):
                    raise
                manifest = json.loads((output / 'capture/manifest.json').read_text())
                assert manifest['status'] == 'incomplete' and manifest['captured_ticks'] == 6, manifest
            else:
                assert not corrupt, 'Cyclic guest unit list was accepted'
        finally:
            gdb.close()


def main():
    nro = Path(subprocess.check_output([sys.executable, str(ROOT / 'scripts/dev/make-oracle-probe.py')], text=True).strip())
    logs = ROOT / '.local/logs'
    logs.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='oracle-', dir=logs))
    (logs / 'oracle-result.json').write_text(json.dumps({'result': 'RUNNING', 'artifacts': str(output)}))
    for name, corrupt in [('first', False), ('second', False), ('fault', True)]:
        print(f'Running {name}: {output / name}', flush=True)
        run(nro, output / name, corrupt)
    first = load_ticks(output / 'first/capture/ticks.jsonl')
    second = load_ticks(output / 'second/capture/ticks.jsonl')
    code, comparison = compare(first, second)
    assert code == 0 and comparison['common_ticks'] == 25, comparison
    assert all(row['n'] == [1] * 5 for row in first.values())
    assert all(len({row['h'][slot] for row in first.values()}) == 25 for slot in range(6))
    # A changed item hash must identify the exact first divergent tick and class.
    altered = json.loads(json.dumps(list(second.values())))
    altered[12]['h'][4] = '0000000000000000'
    code, difference = compare(first, {row['tick']: row for row in altered})
    assert code == 1 and difference['differences']['h[4]']['tick'] == 12, difference
    assert compare(dict(list(first.items())[:9]), dict(list(second.items())[:9]))[0] == 3
    report = {'result': 'PASS', 'scope': 'original synthetic NRO, not D2R game validation',
        'nro_sha256': hashlib.sha256(nro.read_bytes()).hexdigest(), 'comparison': comparison,
        'checks': ['HID shared-memory buttons and axes', 'HID release', 'invalid input rejection',
                   'screenshot no-frame timeout', '25 ticks x 2 independent emulator processes',
                   'all five unit classes and global seed', 'changed hash identifies tick/class',
                   'too few ticks rejected', 'guest unit-list cycle fails with incomplete manifest'],
        'artifacts': str(output)}
    (logs / 'oracle-result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        path = ROOT / '.local/logs/oracle-result.json'
        result = json.loads(path.read_text()) if path.exists() else {}
        result.update(result='FAIL', error=str(error))
        path.write_text(json.dumps(result, indent=2) + '\n')
        raise
