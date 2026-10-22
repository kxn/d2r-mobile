"""Read committed original game-state JSONL records; no polling or device setup."""
import json

STATE_FILE = 'files/runtime/game-state.jsonl'
_END = '__D2R_GAME_STATE_END__'


def latest_complete_state(text):
    # game_state.c writes a record in several chunks, then '}\n' and fflush.
    # A syntactically valid but unterminated final object is not committed yet.
    complete, newline, _partial = text.rpartition('\n')
    if not newline:
        raise ValueError('original game-state has no complete newline-terminated record')
    value = json.loads(complete.rsplit('\n', 1)[-1])
    if not isinstance(value, dict):
        raise ValueError('original game-state complete record is not an object')
    return value  # Never search backward past a malformed complete record.


def read_state_text(app, full=False):
    # Original Device.app strips stdout. A fixed suffix preserves the writer's
    # final newline; no change to Device, lifecycle or its failure handling.
    command = 'cat "$1"' if full else 'tail -n 2 "$1"'
    text = app('sh', '-c', 'set -e; ' + command + '; printf %s ' + _END, 'sh', STATE_FILE)
    if not text.endswith(_END):
        raise ValueError('original game-state read terminator missing')
    return text[:-len(_END)]
