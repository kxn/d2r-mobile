"""Offline identity/coverage gates for original ordinary-UID batch tool outputs.
No device operation, texture protocol change, producer execution or profiling.
"""
import hashlib
import io
import json
from pathlib import Path
import re
import zipfile


def require(value, message):
    if not value:
        raise ValueError(message)


def source_ready_gate(path, expected_sha, expected_producer, legacy_receipt, hashes):
    require(re.fullmatch('[0-9a-f]{64}', expected_producer), 'explicit producer source SHA required')
    require(hashes.file(path) == expected_sha, 'new producer source-ready changed')
    ready = json.loads(path.read_text())
    require(ready['producer_source_sha256'] == expected_producer and ready['tracked_clean'] is True,
            'new producer source identity differs')
    canonical = ready['canonical']
    require(canonical['source_clean'] is True and canonical['driver_sha256'] == legacy_receipt['driver_sha256'] and
            canonical['native_protocols_unchanged'] == ['DMRSRC1', 'DMNRES1', 'DMNTBL2'],
            'new producer changed driver/protocol contract')
    for item in ready['source_files']:
        hashes.record(item)
    identity = ready['producer_source_identity']
    require(identity['profile'] == 'turnip-coherent-host-astc-v1', 'producer profile differs')
    inputs = identity['CMake_inputs']
    names = ['CMakeLists.txt', 'fixture.c', 'native_import.inc', 'resource_bake.inc',
             'resource_batch.inc', 'resource_format.h', 'sample.comp']
    require([Path(item['path']).name for item in inputs] == names, 'producer CMake inputs differ')
    for item in inputs:
        hashes.record(item)
    content = ''.join(Path(item['path']).name + ':' + item['sha256'] + '\n' for item in inputs)
    content += 'contract-profile:' + identity['profile'] + '\n'
    require(hashlib.sha256(content.encode()).hexdigest() == expected_producer, 'producer source digest does not reproduce CMake identity')
    command = identity['actual_NDK_fixture_command']['command']
    require('DM_NATIVE_RESOURCE_PRODUCER_SHA=' in command and expected_producer in command,
            'NDK command lacks exact producer fingerprint')
    apk = hashes.record(canonical['APK'])
    with zipfile.ZipFile(apk) as archive:
        libraries = {name: hashlib.sha256(archive.read('lib/arm64-v8a/' + name)).hexdigest()
                     for name in canonical['native_libraries']}
        elf = archive.read('lib/arm64-v8a/libdmhost_android.so')
    require(libraries == canonical['native_libraries'] and libraries['libdmhost_android.so'] == canonical['ELF_sha256'] and
            expected_producer.encode() in elf, 'new canonical producer APK/ELF differs')
    return ready


def actual_batches_gate(path, expected_sha, ready, expected_producer, prepared,
                        prepared_sha, legacy_receipt, driver_query, hashes):
    require(hashes.file(path) == expected_sha, 'actual batch receipt index changed')
    index = json.loads(path.read_text())
    require(index.get('schema') == 1 and index.get('complete') is True and
            index['producer_source_sha256'] == expected_producer and
            index['source_pack_manifest_sha256'] == prepared_sha, 'incomplete/mismatched actual batch receipt index')
    require(index.get('results'), 'no actual batch results; native composition remains incomplete')
    canonical = ready['canonical']
    identity = legacy_receipt['identity']['actual_recipe_UUID_payload_input_equal_table']
    uuids = {name: identity[key] for name, key in [('layout_UUID', 'layout_uuid'),
             ('device_UUID', 'device_uuid'), ('driver_UUID', 'driver_uuid')]}
    selected = {entry['id']: entry for entry in prepared['entries']}
    batches = {batch['ordinal']: batch for batch in prepared['batches']}
    seen_batches, covered = set(), {}
    for item in index['results']:
        result_path = hashes.record(item)
        root = result_path.parent
        result = json.loads(result_path.read_text())
        require(result['stage'] == 'actual-turnip-native-resource-batch-owned-passed' and
                not result['cleanup_errors'] and result['original_unchanged'] is True and
                result['fixture_PID_absent'] is True and result['fixture_uninstalled'] is True and
                result['fixture_tmp_absent'] is True and result['owned_clients_stopped'] is True,
                'actual batch failed, unfinished or cleanup incomplete')
        require(result['original_before'] == result['original_after'], 'actual batch altered original production')
        require(result['prepare_manifest_sha256'] == prepared_sha and
                result['APK_sha256'] == canonical['APK']['sha256'] and
                result['native_libraries'] == canonical['native_libraries'] and
                result['driver_sha256'] == legacy_receipt['driver_sha256'] and
                result['controller_sha256'] == ready['ordinary_UID_controller']['sha256'],
                'actual batch canonical producer/driver/source identity differs')
        number = result['batch_index']
        require(number in batches and number not in seen_batches, 'unknown/duplicate actual batch')
        seen_batches.add(number)
        batch = batches[number]
        hashes.record(batch['manifest'])
        require(result['batch_manifest_sha256'] == batch['manifest']['sha256'] and
                result['entry_count'] == len(batch['entry_ids']), 'actual batch manifest/count differs')
        runs = result['runs']
        require(len(runs) == 1 and runs[0]['result'] == 'passed' and runs[0]['first_pid'] > 0 and
                runs[0]['PID_absent_after'] is True and
                all(runs[0]['queried_identity'][key] == value for key, value in uuids.items()),
                'actual batch PID or three UUIDs differ')
        configurations = [row for row in runs[0]['rows'] if row.get('native_image_app_configuration') is True]
        require(len(configurations) == 1 and configurations[0]['uid'] >= 10000 and
                configurations[0]['game_modules_executed'] is False,
                'actual batch lacks ordinary-UID execution evidence')
        expected_files = {'resource-batch-receipts.jsonl', 'resource-batch-complete.json'}
        expected_files |= {'items/' + entry + '/' + leaf for entry in batch['entry_ids']
                           for leaf in ['native-resource.bin', 'native-resource.json', 'native-resource-layout.bin']}
        require(set(result['baked_outputs']) == expected_files, 'actual batch output coverage differs')
        for relative, value in result['baked_outputs'].items():
            require(hashes.file(root / relative) == value, 'actual baked output changed: ' + relative)
        complete = json.loads((root / 'resource-batch-complete.json').read_text())
        require(complete == result['actual_batch_complete'] and complete['complete'] is True and
                complete['entries'] == len(batch['entry_ids']) and complete['producer_source_sha256'] == expected_producer and
                complete['manifest_sha256'] == batch['manifest']['sha256'] and
                complete['GPU_sampling_executed'] is False and complete['game_modules_executed'] is False,
                'actual complete receipt differs')
        receipts = [json.loads(row) for row in (root / 'resource-batch-receipts.jsonl').read_text().splitlines() if row]
        require(len(receipts) == len(batch['entry_ids']), 'actual entry receipt count differs')
        physical = 0
        for ordinal, (entry_id, receipt) in enumerate(zip(batch['entry_ids'], receipts)):
            require(entry_id in selected and entry_id not in covered, 'unknown/duplicate actual entry')
            entry = selected[entry_id]
            require(receipt['entry_id'] == entry_id and receipt['ordinal'] == ordinal and receipt['complete'] is True and
                    receipt['input_sha256'] == entry['input']['sha256'] and
                    receipt['declaration_sha256'] == entry['source']['sha256'] and
                    receipt['producer_source_sha256'] == expected_producer and
                    receipt['original_driver_roundtrip_compressed_blocks_exact'] is True and
                    all(receipt[key] == value for key, value in uuids.items()), 'actual entry source/identity differs')
            baked = root / 'items' / entry_id
            for leaf, field in [('native-resource.bin', 'native_payload_sha256'),
                    ('native-resource.json', 'native_metadata_sha256'), ('native-resource-layout.bin', 'layout_record_sha256')]:
                require(hashes.file(baked / leaf) == receipt[field], 'actual entry three-file receipt differs')
            metadata = json.loads((baked / 'native-resource.json').read_text())
            require(metadata['complete'] is True and metadata['driverID'] == 18 and
                    metadata['producer_source_sha256'] == expected_producer and
                    metadata['input_sha256'] == receipt['input_sha256'] and
                    metadata['declaration_sha256'] == receipt['declaration_sha256'] and
                    metadata['original_driver_roundtrip_compressed_blocks_exact'] is True and
                    all(metadata[key] == value for key, value in uuids.items()), 'actual metadata differs')
            require(metadata['physical_image_bytes'] == receipt['physical_image_bytes'] ==
                    (baked / 'native-resource.bin').stat().st_size, 'actual native payload extent differs')
            wire = (baked / 'native-resource-layout.bin').read_bytes()
            require(len(wire) == 992 and wire[:8] == b'DMNRES1\0' and wire[288:320].hex() == expected_producer and
                    wire[112:160].hex() == ''.join(uuids[key] for key in ['layout_UUID', 'device_UUID', 'driver_UUID']),
                    'actual wire producer/three UUIDs differ')
            covered[entry_id] = dict(baked=str(baked.resolve()), result=str(result_path),
                                    metadata=str((baked / 'native-resource.json').resolve()))
            physical += receipt['physical_image_bytes']
        require(physical == complete['physical_image_bytes'], 'actual batch native total differs')
    require(seen_batches == set(batches) and set(covered) == set(selected),
            'actual batches do not cover complete selection; native composition remains incomplete')
    require(str(driver_query.resolve()) in {item['metadata'] for item in covered.values()},
            'pack driver query must be a current actual batch output, never older single-material metadata')
    return covered
