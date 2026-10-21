#!/usr/bin/env python3
"""Offline official Simpleperf/Perfetto reports, one graph set per A/A window."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys

R = Path('/home/dl/d2r-mobile')
NDK = R / '.local/android-sdk/ndk/28.2.13676358'
sys.path.insert(0, str(NDK / 'simpleperf'))
from simpleperf_report_lib import ReportLib


def contained(sample_first, sample_last, trace_first, trace_last):
    return sample_first <= trace_first < trace_last <= sample_last


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--binary-cache', type=Path, required=True)
    parser.add_argument('--expected-build-id', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.capture = args.capture.resolve()
    args.binary_cache = args.binary_cache.resolve()
    args.output = args.output.resolve()
    meta = json.loads((args.capture / 'official-cpu-capture.json').read_text())
    assert meta['complete'] and len(meta['AA_windows']) == 3
    callgraph = meta.get('AA_callgraph', 'dwarf')
    assert ((callgraph == 'dwarf' and meta['AA_DWARF_bytes'] == 49152 and meta['AA_keep_failed_debug_info']) or
            (callgraph == 'fp' and meta['AA_DWARF_bytes'] is None and not meta['AA_keep_failed_debug_info']))
    args.output.mkdir(exist_ok=False)
    (args.output / 'binary_cache').symlink_to(args.binary_cache.resolve(), target_is_directory=True)
    commands, windows = [], []
    env = os.environ.copy()
    env['PYTHONPATH'] = str(R / '.local/tools/simpleperf-python-deps') + os.pathsep + env.get('PYTHONPATH', '')

    def execute(argv, stdout, stderr):
        with stdout.open('w') as out, stderr.open('w') as err:
            result = subprocess.run(argv, cwd=args.output, env=env, stdout=out, stderr=err)
        commands.append({'argv': argv, 'returncode': result.returncode})
        assert result.returncode == 0, 'official offline tool failed; retain stderr'

    for row in meta['AA_windows']:
        expected = 'fp' if callgraph == 'fp' else 'dwarf,49152'
        assert row['command'][row['command'].index('--call-graph') + 1] == expected
        if callgraph == 'fp':
            assert not any(v.startswith(('--post-unwind', '--keep-failed-unwinding', '--no-callchain-joiner'))
                           for v in row['command'])
        data = Path(row['data'])
        assert hashlib.sha256(data.read_bytes()).hexdigest() == row['data_sha256']
        report = ReportLib()
        report.SetRecordFile(str(data))
        report.SetSymfs(str(args.binary_cache))
        info = report.MetaInfo()
        assert info['clockid'] == 'monotonic', 'unsupported sample clock; no raw-ns comparison'
        times, nvn_times, app_ids = [], [], set()
        while (sample := report.GetNextSample()):
            assert sample.pid == meta['owned_PID'] and report.GetEventOfCurrentSample().name == 'cpu-clock:u'
            times.append(int(sample.time))
            if sample.thread_comm == 'nvn-translate':
                nvn_times.append(int(sample.time))
            symbol = report.GetSymbolOfCurrentSample()
            chain = report.GetCallChainOfCurrentSample()
            for item in [symbol] + [chain.entries[i].symbol for i in range(chain.nr)]:
                if 'libdmhost_android.so' in item.dso_name:
                    app_ids.add(report.GetBuildIdForPath(item.dso_name).removeprefix('0x'))
        report.Close()
        assert app_ids == {args.expected_build_id} and times and nvn_times
        name = 'AA-record-' + str(row['ordinal'])
        trace = Path(row['Perfetto']['report']).with_suffix('.pftrace')
        query = args.output / (name + '-clock-map.sql')
        query.write_text("SELECT * FROM clock_snapshot;\nSELECT start_ts,end_ts,"
                         "to_monotonic(start_ts) AS monotonic_start_ns,"
                         "to_monotonic(end_ts) AS monotonic_end_ns FROM trace_bounds;\n")
        output = query.with_suffix('.csv')
        execute([str(R / '.local/tools/perfetto/trace_processor'), 'query', '-f', str(query), str(trace)],
                output, query.with_suffix('.stderr'))
        tables = [list(csv.DictReader(io.StringIO(t))) for t in output.read_text().strip().split('\n\n')]
        mapped = tables[-1][0]
        a, b = int(mapped['monotonic_start_ns']), int(mapped['monotonic_end_ns'])
        window = {'ordinal': row['ordinal'], 'callgraph': callgraph, 'DWARF_bytes': meta['AA_DWARF_bytes'], 'actual_meta': info, 'actual_APP_BuildIDs': sorted(app_ids),
                  'PID_samples': len(times), 'NvN_samples': len(nvn_times),
                  'PID_sample_MONOTONIC_ns': [min(times), max(times)],
                  'NvN_sample_MONOTONIC_ns': [min(nvn_times), max(nvn_times)],
                  'Perfetto_trace_clock_ns': [int(mapped['start_ts']), int(mapped['end_ts'])],
                  'Perfetto_official_to_monotonic_ns': [a, b],
                  'full_trace_inside_PID_samples': contained(min(times), max(times), a, b),
                  'full_trace_inside_NvN_samples': contained(min(nvn_times), max(nvn_times), a, b),
                  'clock_map': str(output), 'original_bracket': row['bracket']}
        windows.append(window)
        for label, filters in [('nvn-translate', ['--comm', 'nvn-translate']), ('all-threads', [])]:
            execute([sys.executable, str(NDK / 'simpleperf/inferno/inferno.py'), '--skip_collection',
                     '--record_file', str(data), '--symfs', str(args.binary_cache), *filters,
                     '--no_browser', '-o', str(args.output / (name + '-' + label + '.html'))],
                    args.output / (name + '-' + label + '.stdout'), args.output / (name + '-' + label + '.stderr'))
        execute([sys.executable, str(NDK / 'simpleperf/report_html.py'), '-i', str(data),
                 '--comm', 'nvn-translate', '--no_browser', '--ndk_path', str(NDK), '-j', '2',
                 '-o', str(args.output / (name + '-callgraph.html'))],
                args.output / (name + '-callgraph.stdout'), args.output / (name + '-callgraph.stderr'))
    result = {'status': 'OFFICIAL_OFFLINE_AA_REPORTS_COMPLETE_NOT_OPTIMIZATION_GAIN', 'windows': windows,
              'overlap_accepted': all(w['full_trace_inside_PID_samples'] and w['full_trace_inside_NvN_samples'] for w in windows),
              'sampling_callgraph': callgraph, 'cross_method_DWARF_FP_equivalence_proven': False,
              'same_workload_or_reproducibility_proven': False,
              'boundary': 'Clock overlap is necessary, not identical workload, precision, full unwinding or zero sampling disturbance. '
                          'Compare the two Perfetto-only bookends with three concurrent traces and original state/cap/publicthermal endpoints. '
                          'Keep each graph separately; no pooled graph hides window drift.',
              'commands': commands}
    (args.output / 'official-AA-analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    assert result['overlap_accepted'], 'capture finished but intended overlap is not proven by official timestamps'


if __name__ == '__main__':
    main()
