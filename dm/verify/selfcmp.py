#!/usr/bin/env python3
"""Compare tick_state_format.md streams; never accept broken or insufficient data."""
import argparse
import json
from pathlib import Path
import re


def load_ticks(path):
    rows = {}
    previous = None
    with Path(path).open() as stream:
        for number, line in enumerate(stream, 1):
            try:
                row = json.loads(line)
                tick = row['tick']
                if type(tick) is not int or not 0 <= tick <= 0xffffffff:
                    raise ValueError('invalid tick')
                if previous is not None and tick != previous + 1:
                    raise ValueError(f'nonconsecutive tick {previous} -> {tick}')
                if type(row['bad']) is not int or row['bad'] != 0:
                    raise ValueError('bad state read')
                counts, hashes = row['n'], row['h']
                if not isinstance(counts, list) or len(counts) != 5 or any(type(n) is not int or n < 0 for n in counts):
                    raise ValueError('invalid unit counts')
                if not isinstance(hashes, list) or len(hashes) != 6 or any(not isinstance(h, str) or not re.fullmatch('[0-9a-f]{16}', h) for h in hashes):
                    raise ValueError('invalid hashes')
            except (ValueError, KeyError, TypeError) as error:
                raise ValueError(f'{path}:{number}: {error}') from error
            rows[tick] = row
            previous = tick
    return rows


def compare(left, right, allow_partial=False):
    common = sorted(left.keys() & right.keys())
    report = {'common_ticks': len(common), 'left_ticks': len(left), 'right_ticks': len(right)}
    if len(common) < 10:
        return 3, dict(report, status='insufficient', reason='at least 10 common ticks required')
    if not allow_partial and left.keys() != right.keys():
        return 2, dict(report, status='invalid', reason='tick ranges differ; use --allow-partial explicitly')
    differences = {}
    for tick in common:
        for field in ('n', 'h'):
            for slot, (a, b) in enumerate(zip(left[tick][field], right[tick][field])):
                key = f'{field}[{slot}]'
                if a != b and key not in differences:
                    differences[key] = {'tick': tick, 'left': a, 'right': b}
    return int(bool(differences)), dict(report, status='different' if differences else 'match',
        first_tick=common[0], last_tick=common[-1], partial=left.keys() != right.keys(), differences=differences)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left')
    parser.add_argument('right')
    parser.add_argument('--allow-partial', action='store_true')
    args = parser.parse_args()
    try:
        code, report = compare(load_ticks(args.left), load_ticks(args.right), args.allow_partial)
    except (OSError, ValueError) as error:
        code, report = 2, {'status': 'invalid', 'reason': str(error)}
    print(json.dumps(report, indent=2))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
