#!/usr/bin/env python3
"""Record live tick hashes with explicit entry address and failure metadata.

Does not fix game seed, clock, save or input ordering; callers must control these.
Uses the original Switch state layout. No private upstream recording script required.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time

from swgdb import Gdb
from swtick import Reader, tick_line


class StrictReader(Reader):
    def rd(self, address, size):
        if not address:
            return None  # Absent optional pointer is valid in the published format.
        self.n += 1
        data = self.g.read(address, size)
        if len(data) != size:
            raise IOError(f'Short memory read at {address:#x}: {len(data)} != {size}')
        return data


def record(gdb, entry, count, directory, timeout=30, on_tick=None):
    if count < 1:
        raise ValueError('count must be positive')
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    manifest = {'status': 'incomplete', 'requested_ticks': count, 'captured_ticks': 0,
                'entry': hex(entry), 'schema': 'tick_state_format.md',
                'deterministic_game_setup': 'not established by this recorder'}
    installed = False
    last = None
    duplicates = 0
    try:
        stop = gdb.interrupt()
        gdb.select(stop)
        # step_over relies on an ordinary fall-through instruction at entry.
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM, CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET, CS_GRP_INT
        code = gdb.read(entry, 4)
        decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        decoder.detail = True
        instruction = next(decoder.disasm(code, entry), None)
        if instruction is None or any(instruction.group(group) for group in (CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET, CS_GRP_INT)):
            raise ValueError('Entry must start with an ordinary fall-through instruction')
        manifest['entry_instruction'] = code.hex()
        if not gdb.set_bp(entry):
            raise RuntimeError('Could not install tick breakpoint')
        installed = True
        with (directory / 'ticks.jsonl').open('x') as stream:
            while manifest['captured_ticks'] < count:
                if gdb.cont() != b'OK':
                    raise RuntimeError('Continue failed')
                stop = gdb.wait_stop(timeout=timeout)
                if gdb.reg(32) != entry:
                    raise RuntimeError(f'Unexpected stop at {gdb.reg(32):#x}')
                game = gdb.reg(0)
                row, units = tick_line(StrictReader(gdb), game)
                tick = row['tick']
                if row['bad']:
                    raise ValueError(f'Invalid unit list at tick {tick}: bad={row["bad"]}')
                if tick == last:
                    duplicates += 1
                    if duplicates > count * 10:
                        raise RuntimeError('Too many repeated stops without tick progress')
                else:
                    if last is not None and tick != last + 1:
                        raise ValueError(f'Tick gap or rewind: {last} -> {tick}')
                    if on_tick is not None:
                        on_tick(gdb, tick, game, units)
                    stream.write(json.dumps(row) + '\n')
                    stream.flush()
                    manifest['captured_ticks'] += 1
                    last = tick
                if manifest['captured_ticks'] < count and not gdb.step_over(entry, stop):
                    raise RuntimeError('Could not step over tick entry')
        manifest['status'] = 'complete'
        manifest['sha256'] = hashlib.sha256((directory / 'ticks.jsonl').read_bytes()).hexdigest()
        return manifest
    except Exception as error:
        manifest['error'] = str(error)
        raise
    finally:
        manifest['duplicate_stops'] = duplicates
        manifest['finished_unix'] = time.time()
        (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if installed:
            gdb.clear_bp(entry)
            # Leave the guest paused for the next controlled operation.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=55571)
    parser.add_argument('--tick-address', type=lambda text: int(text, 0), required=True)
    parser.add_argument('--ticks', type=int, default=25)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    gdb = Gdb(port=args.port)
    try:
        print(json.dumps(record(gdb, args.tick_address, args.ticks, args.out), indent=2))
    finally:
        gdb.close()


if __name__ == '__main__':
    main()
