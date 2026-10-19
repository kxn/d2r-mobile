#!/usr/bin/env python3
"""Client for our Ryujinx monitor extension (one local GDB client at a time)."""
import argparse
import json
from pathlib import Path
import time

from swgdb import Gdb


def command(gdb, text):
    response = json.loads(gdb.monitor('d2r ' + text))
    if 'error' in response:
        raise RuntimeError(response['error'])
    return response


def set_input(gdb, buttons=0, lx=0, ly=0, rx=0, ry=0, timeout=5):
    generation = command(gdb, f'input {buttons:x} {lx} {ly} {rx} {ry}')['generation']
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = command(gdb, 'input-state')
        if state['applied'] == generation and state['style'] != 0:
            return state
        time.sleep(0.01)
    raise TimeoutError('Input was not published to a connected HID shared-memory ring')


def save_frame(state, target):
    from PIL import Image
    raw = Path(state['path'])
    data = raw.read_bytes()
    if len(data) != state['width'] * state['height'] * 4:
        raise ValueError('Capture byte count does not match its dimensions')
    img = Image.frombytes('RGBA', (state['width'], state['height']), data,
                          'raw', 'BGRA' if state['bgra'] else 'RGBA')
    if state['flipX']:
        img = img.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    if state['flipY']:
        img = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    img.save(target, format='PNG')
    raw.unlink()


def screenshot(gdb, target, timeout=10):
    # Caller must let the guest render. A paused/non-rendering guest times out.
    deadline = time.monotonic() + timeout
    try:
        while True:
            request = command(gdb, 'screenshot')
            if request['status'] != 'not-ready':
                break
            if time.monotonic() >= deadline:
                raise TimeoutError('Renderer has not initialized; screenshot was not produced')
            time.sleep(0.05)
        while time.monotonic() < deadline:
            state = command(gdb, 'shot-status')
            if state['status'] == 'ready' and state['id'] == request['id']:
                save_frame(state, target)
                return state
            time.sleep(0.05)
        raise TimeoutError('No new presented frame; screenshot was not produced')
    finally:
        command(gdb, 'shot-cancel')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=55571)
    sub = parser.add_subparsers(dest='action', required=True)
    sub.add_parser('info')
    shot = sub.add_parser('screenshot')
    shot.add_argument('output', type=Path)
    shot.add_argument('--timeout', type=float, default=10)
    press = sub.add_parser('press')
    press.add_argument('buttons', type=lambda s: int(s, 0))
    press.add_argument('--seconds', type=float, default=0.2)
    args = parser.parse_args()
    gdb = Gdb(port=args.port)
    try:
        if args.action == 'info':
            print(json.dumps(command(gdb, 'info'), indent=2))
        elif args.action == 'screenshot':
            screenshot(gdb, args.output, args.timeout)
        else:
            if not 0 <= args.seconds <= 60:
                parser.error('--seconds must be between 0 and 60')
            print(json.dumps(set_input(gdb, args.buttons)))
            time.sleep(args.seconds)
            set_input(gdb)
    finally:
        command(gdb, 'release')
        gdb.close()


if __name__ == '__main__':
    main()
