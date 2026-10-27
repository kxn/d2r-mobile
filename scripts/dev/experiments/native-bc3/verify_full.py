#!/usr/bin/env python3
"""Verify all prepared BC3 wires using the real frozen producer's CPU parser."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

spec = importlib.util.spec_from_file_location('bc3_full', Path(__file__).with_name('prepare_full.py'))
full = importlib.util.module_from_spec(spec)
spec.loader.exec_module(full)


def run(a):
    full.require(not a.output.exists(), 'new verification output required')
    ready = full.producer_gate(a.producer)
    prepared = json.loads(a.prepared.read_text())
    full.require(prepared['complete'] and len(prepared['entries']) == full.EXPECTED and
                 prepared['producer_source_sha256'] == full.PRODUCER,
                 'complete same-producer 6217 preparation required')
    producer = a.producer.resolve() / 'host/native_image_payload'
    binary = full.record(producer)
    a.output.mkdir(parents=True)
    rows, names = [], []
    for b in prepared['batches']:
        stage = a.output / f"{b['ordinal']:04d}"
        stage.mkdir()
        source = Path(b['manifest']['path'])
        full.require(full.record(source) == b['manifest'], 'batch file changed')
        (stage / 'resource-batch.tsv').symlink_to(source)
        (stage / 'items').symlink_to(a.prepared.resolve().parent / 'items', target_is_directory=True)
        result = subprocess.run([str(producer), '--verify-resource-batch', str(stage.resolve())],
                                capture_output=True, text=True)
        (stage / 'stdout.jsonl').write_text(result.stdout)
        (stage / 'stderr.txt').write_text(result.stderr)
        full.require(result.returncode == 0, 'real producer rejected batch ' + str(b['ordinal']))
        parsed = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        full.require(any(row.get('test') == 'native-resource-batch-input' and
                         row.get('result') == 'passed' and row.get('GPU_executed') is False
                         for row in parsed), 'batch lacks real host-only PASS')
        names.extend(b['entry_ids'])
        rows.append({'ordinal': b['ordinal'], 'entry_count': len(b['entry_ids']),
                     'stdout': full.record(stage / 'stdout.jsonl'),
                     'stderr': full.record(stage / 'stderr.txt')})
    full.require(names == [r['id'] for r in prepared['entries']], 'batch coverage/order differs')
    for row in prepared['entries']:
        for key in ['source', 'texture', 'input', 'prepare_report']:
            full.require(full.record(Path(row[key]['path'])) == row[key],
                         'prepared payload changed: ' + row['id'] + '/' + key)
    full.require(full.record(producer) == binary and full.producer_gate(a.producer) == ready,
                 'producer changed during host verification')
    report = {'complete': True, 'entries': len(names), 'batches': len(rows),
              'producer_source_sha256': full.PRODUCER, 'producer_source_ready': ready,
              'host_producer': binary, 'prepared': full.record(a.prepared),
              'all_declared_files_rehashed': True, 'actual_C_batch_parser_passed': True,
              'GPU_executed': False, 'game_executed': False,
              'native_layout_query_pending': True, 'results': rows,
              'tool': full.record(__file__)}
    full.save(a.output / 'verification.json', report)
    print(json.dumps({k: v for k, v in report.items() if k != 'results'}, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--producer', type=Path, required=True)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    run(p.parse_args())
