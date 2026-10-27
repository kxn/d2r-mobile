#!/usr/bin/env python3
"""Compile the real NVN recorder and replay state case, compare OFF/ON traces.

The platform/queue and memory-supply callbacks are owned fixtures. Command
allocation, headers, Begin/End, CALL/JUMP, six public state binders and Vulkan's
state application are read verbatim from production. No GPU/device required.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'dm/gfx/nvn/hle_nvn.c').read_text()
    # These complete production sections preserve callbacks and real encoding.
    start = source.index('#define CB(c)')
    end = source.index('static void copy_view(', start)
    recorder = source[start:end]
    bind_start = source.index('static void bind_state(')
    bind_end = source.index('NVN(nvnCommandBufferBindVertexAttribState', bind_start)
    recorder += source[bind_start:bind_end]
    for name in ['nvnCommandBufferDrawArrays', 'nvnCommandBufferClearColor',
                 'nvnCommandBufferBarrier', 'nvnCommandBufferBindUniformBuffer',
                 'nvnCommandBufferSetStencilRef']:
        begin = source.index('NVN(' + name + ',')
        # Match actual function-body braces; these small setters contain no
        # quoted/comment braces. Fail rather than silently use another body.
        brace = source.index('{', begin)
        depth = 1
        i = brace + 1
        while depth:
            depth += (source[i] == '{') - (source[i] == '}')
            i += 1
        recorder += source[begin:i] + '\n'
    state_source = (ROOT / 'dm/gfx/vulkan/nvn_vk_state.c').read_text()
    begin = state_source.index('  case OP_BIND_STATE: {')
    end = state_source.index('  case OP_BIND_VATTR:', begin)
    apply_case = state_source[begin:end]
    with tempfile.TemporaryDirectory(prefix='nvn-record-state-') as td:
        temp = Path(td)
        (temp / 'recording-under-test.inc').write_text(recorder)
        (temp / 'replay-state-under-test.inc').write_text(apply_case)
        command = [args.cc, '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-function', '-pthread',
                   '-Wno-misleading-indentation',  # existing compact cs_dump formatting
                   '-I' + str(ROOT / 'dm/gfx/nvn/include'),
                   '-I' + str(ROOT / 'dm/gfx/nvn'), '-I' + str(temp),
                   str(ROOT / 'dm/gfx/tests/t_record_state.c'), '-o', str(temp / 'test')]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        results = []
        for enabled in [False, True]:
            env = dict(os.environ, DM_NVN_RECORD_STATE_CACHE=str(int(enabled)))
            env.pop('DM_NVN_CMDSTAT', None)
            output = subprocess.check_output([str(temp / 'test')], env=env, text=True)
            observations = [line for line in output.splitlines() if line.startswith('OBS ')]
            assert len(observations) >= 20, output
            results.append(observations)
            print(output, end='')
        assert results[0] == results[1], 'OFF/ON changed replayed draw or side-effect observations'
        print('PASS: actual encoder/replay OFF and ON observations match; exact operation counts checked in fixture')


if __name__ == '__main__':
    main()
