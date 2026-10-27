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
    parser.add_argument('--extra', action='store_true', help='incremental stencil/layout candidate; fixed cache remains enabled in both arms')
    args = parser.parse_args()
    source = (ROOT / 'dm/gfx/nvn/hle_nvn.c').read_text()
    # These complete production sections preserve callbacks and real encoding.
    start = source.index('#define CB(c)')
    end = source.index('static void copy_view(', start)
    recorder = source[start:end]
    bind_start = source.index('static void bind_state(')
    bind_end = source.index('NVN(nvnCommandBufferBindVertexAttribState', bind_start)
    recorder += source[bind_start:bind_end]
    begin = source.index('static void record_stencil(')
    end = source.index('NVN(nvnCommandBufferSetStencilValueMask', begin)
    recorder += source[begin:end]
    for name in ['nvnCommandBufferDrawArrays', 'nvnCommandBufferClearColor',
                 'nvnCommandBufferBarrier', 'nvnCommandBufferBindUniformBuffer',
                 'nvnCommandBufferSetStencilRef', 'nvnCommandBufferSetStencilMask',
                 'nvnCommandBufferSetStencilValueMask', 'nvnCommandBufferBindVertexAttribState',
                 'nvnCommandBufferBindVertexStreamState']:
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
    extra_case = state_source[state_source.index('  case OP_BIND_VATTR:'):state_source.index('  case OP_BIND_VBUF:')]
    extra_case += state_source[state_source.index('  case OP_STENCIL:'):state_source.index('  case OP_POLY_OFFSET:')]
    with tempfile.TemporaryDirectory(prefix='nvn-record-state-') as td:
        temp = Path(td)
        (temp / 'recording-under-test.inc').write_text(recorder)
        (temp / 'replay-state-under-test.inc').write_text(apply_case)
        (temp / 'replay-extra-under-test.inc').write_text(extra_case)
        command = [args.cc, '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-function', '-pthread',
                   '-Wno-misleading-indentation',  # existing compact cs_dump formatting
                   '-I' + str(ROOT / 'dm/gfx/nvn/include'),
                   '-I' + str(ROOT / 'dm/gfx/nvn'), '-I' + str(temp),
                   str(ROOT / ('dm/gfx/tests/t_record_state_extra.c' if args.extra else 'dm/gfx/tests/t_record_state.c')), '-o', str(temp / 'test')]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True)
        results = []
        for enabled in [False, True]:
            env = dict(os.environ, DM_NVN_RECORD_STATE_CACHE=str(int(enabled)))
            env['DM_NVN_RECORD_STATE_EXTRA_CACHE'] = '0'
            if args.extra:
                env['DM_NVN_RECORD_STATE_CACHE'] = '1'
                env['DM_NVN_RECORD_STATE_EXTRA_CACHE'] = str(int(enabled))
            env.pop('DM_NVN_CMDSTAT', None)
            output = subprocess.check_output([str(temp / 'test')], env=env, text=True)
            observations = [line for line in output.splitlines() if line.startswith('OBS ')]
            assert len(observations) >= 20, output
            results.append(observations)
            print(output, end='')
        assert results[0] == results[1], 'OFF/ON changed replayed draw or side-effect observations'
        if args.extra:
            env = dict(os.environ, DM_NVN_RECORD_STATE_CACHE='0', DM_NVN_RECORD_STATE_EXTRA_CACHE='1')
            subprocess.run([str(temp / 'test'),'parent-disabled'], env=env, check=True)
            env['DM_NVN_RECORD_STATE_CACHE']='1'
            env.pop('DM_NVN_RECORD_STATE_EXTRA_CACHE')
            subprocess.run([str(temp / 'test'),'extra-default'], env=env, check=True)
        print('PASS: actual encoder/replay OFF and ON observations match; exact operation counts checked in fixture; incremental_extra=' + str(args.extra))


if __name__ == '__main__':
    main()
