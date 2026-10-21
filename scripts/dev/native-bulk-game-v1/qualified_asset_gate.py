"""Bounded original-game experiment for the reviewed32 ASTC6 replacements only.

This authenticates existing composer, packer and real-NVN evidence. It does not
change their historical deploy_allowed/game_acceptance flags or encode assets.
"""
from pathlib import Path
import struct

RUNTIME = 'd14808f591b0de60c241340c3f5c5df708f4e36b'
ASSET_REVIEW = 'e2cb6ad4fcc8b45eb379aac8e7ce4236abcbfe57de17d547e8c805ad01a8ef1c'
DUAL_REVIEW = '9a510e8ffcb211c7c4227a69fffb7661a20c48c939ecbcc2cd8e071e32982c1b'
COMPOSITION = '8676e748175eecbc3bdcfc42038e428775cc5917c3293eec56bc5335f55806f3'
TABLE = '030ba91c9aecbecf4a2c335cb7da168b8753e24681a02d9a5466b6a7af5c5b77'
SOURCE_READY = '57a49fa74674961c6352dfa83352489c204ae0f0f3ecd6364ff99ad7ed04b4e3'
OLD_ROOT = Path('/home/dl/d2r-mobile/.local/build/turnip-native-bulk-6217-f830-v1')
OLD_PACK = '8f9562ee202431da15e112577d50762d17db384577cfafdefe8f9930ab1f423e'
OLD_TABLE = '90ac8aa32456fb3be44a478e38f21f84f33cf5345c5a08ac6308b3453ec76113'
NEW_PACK = '395107559ddf96fa8a95573e0b525fedc8256d7475152095f54f8167d83fe7c0'


def validate_membership(old, new, old_table, new_table, changed, require):
    """Compare original strict-packer outputs; no new layout interpretation."""
    expected = {r['id']: r for r in changed}
    require(len(expected) == len(changed) == 32, 'exact32 replacements required')
    require(len(old) == len(new) == 6217 and
            len(old_table) == len(new_table) == 160 + 6217 * (1040 + 16),
            'complete same6217 family required')
    for i, (before, after) in enumerate(zip(old, new)):
        number = i + 1
        require(before['id'] == after['id'] == number and
                before['logical'] == after['logical'], 'serialized ID/order/name changed')
        offset = 160 + i * 1040
        a, b = old_table[offset:offset + 1040], new_table[offset:offset + 1040]
        family = 160 + 6217 * 1040 + i * 16
        if number not in expected:
            require(before == after and a == b and
                    old_table[family:family + 16] == new_table[family:family + 16],
                    'unselected native descriptor/recipe/family changed')
        else:
            row = expected[number]
            old_format = struct.unpack_from('<I', a, 48 + 32)[0]
            new_format = struct.unpack_from('<I', b, 48 + 32)[0]
            require(after['logical'] == row['logical'] and
                    before['image_size'] == row['old_native_bytes'] and
                    after['image_size'] == row['new_native_bytes'] and
                    (old_format, new_format) in ((157, 165), (158, 166)),
                    'replacement identity/size/qualified format differs')


def validate(p, pinned, read, require):
    mode = p['qualified_native_asset_validation']
    require(set(mode) == {'root_asset_review', 'dual_NVN_review'} and
            p.get('native_candidate_validation') and p['runtime_commit'] == RUNTIME and
            'sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p, 'exact d148 bounded asset experiment only')
    require(mode['root_asset_review']['sha256'] == ASSET_REVIEW and
            mode['dual_NVN_review']['sha256'] == DUAL_REVIEW,
            'actual root asset and dual-format reviews required')
    review = read(pinned(mode['root_asset_review']))
    require(review['status'] == 'ROOT_REVIEWED_ALLOW_BOUNDED_MIXED32_ASTC6_GAME_QUALIFICATION'
            and review['allow_bounded_original_game_qualification'] is True and
            review['game_acceptance'] is False and review['production_default_enable'] is False and
            review['assets_changed'] is True and review['CPU_matched_asset_AB'] is False and
            review['performance_claim'] is False and review['historical_receipts_unchanged'] is True and
            review['entry_count'] == 6217 and review['replacement_count'] == 32 and
            review['unchanged_entries_exact'] == 6185 and
            review['cache_unchanged_entries_exact'] == 31040,
            'root bounded asset authorization differs')
    require(review['dual_NVN_actual_review'] == mode['dual_NVN_review'] and
            review['source_ready']['sha256'] == SOURCE_READY and
            review['composition'] == p['phases'][1]['composition'] and
            review['composition']['sha256'] == COMPOSITION and
            p['phases'][1]['native_table']['sha256'] == review['native_table_sha256'] == TABLE,
            'mixed composition/table/provenance differs')
    pinned(review['source_pin_verification']); pinned(review['output_pin_verification'])
    source = read(pinned(review['source_ready']))
    for key in ('prepared_manifest', 'actual_complete', 'actual_receipt_index'):
        pinned(source[key])
    composition = p['phases'][1]['composition_data']
    require(composition['deploy_allowed'] is False and composition['game_acceptance'] is False and
            composition['replaced_entries'] == 32 and composition['unchanged_native_entries'] == 6185 and
            composition['unchanged_cache_entries'] == 31040 and
            composition['original_CASC_roundtrip'] is True and
            composition['upstream_UI_preserved'] is True and
            composition['original_packer_unchanged'] is True and
            composition['source_or_payload_byte_patch'] is False and
            composition['game_binary_modified'] is False and
            all(composition['original_TVFS_proof'][key] is True for key in
                ('VFS_only_selected_CFT_fields_changed', 'root_only_VFS_reference_changed',
                 'config_only_VFS_keys_sizes_changed')),
            'original CASC/TVFS/UI and historical status contract differs')
    # One complete host-side check, not one large-archive hash per texture.
    outputs = read(pinned(source['validated_all_output_pins']))
    root = Path(outputs['output_root']).resolve()
    require(outputs['complete'] is True and len(outputs['files']) == 12447 and
            Path(p['phases'][1]['overlay_root']).resolve() == root / 'native-overlay/romfs_overlay' and
            Path(p['phases'][1]['native_table']['path']).resolve() == root / 'native-overlay/files/native-layouts.bin',
            'complete reviewed output root required')
    seen = set()
    for row in outputs['files']:
        path = Path(row['path']).resolve()
        require(path.is_relative_to(root) and path not in seen,
                'duplicate or outside composed output')
        seen.add(path)
        verified = pinned(row)
        require(verified.stat().st_size == row['bytes'], 'composed output size changed')
    new_pack = read(pinned({'path': str(root / 'native-pack/native-pack.json'), 'sha256': NEW_PACK}))
    old_pack = read(pinned({'path': str(OLD_ROOT / 'native-pack/native-pack.json'), 'sha256': OLD_PACK}))
    old_table = pinned({'path': str(OLD_ROOT / 'native-overlay/files/native-layouts.bin'), 'sha256': OLD_TABLE})
    validate_membership(old_pack['entries'], new_pack['entries'], old_table.read_bytes(),
                        pinned(p['phases'][1]['native_table']).read_bytes(), review['changed'], require)
    for name in ('ctrl_touch', 'touch_strings.tsv', 'data/00-00000007.data'):
        require(p['phases'][0]['overlay'][name] == p['phases'][1]['overlay'][name],
                'upstream UI/original archive7 differs')
    dual = read(pinned(mode['dual_NVN_review']))
    require(dual['game_acceptance'] is False and dual['performance_claim'] is False and
            set(dual['formats']) == {'165', '166'}, 'dual NVN scope changed')
    for fmt in ('165', '166'):
        row = dual['formats'][fmt]
        actual = read(pinned(row['receipt']))
        require(actual['stage'] == 'actual-qualified-ASTC6-original-NVN-native-window-family-owned-passed' and
                actual['cleanup_errors'] == [] and actual['original_unchanged'] is True and
                row['terminal']['rc'] == 0 and row['terminal']['actual_ASTC_GPU'] is True and
                row['oracle']['format'] == int(fmt) and row['oracle']['checks'] == 447 and
                row['oracle']['mips'] == 9 and row['oracle']['full_all_mips'] is True and
                row['image_only']['checks_added'] == 36 and row['previous_writer']['checks'] == 40 and
                all(row[k]['result'] == 'passed' for k in ('oracle', 'image_only', 'previous_writer')),
                'actual ASTC6 original NVN 447/36/40 qualification incomplete')
    # The165 original controller process exit=1 is an independently fixed CLI
    # whitelist defect, not a fabricated exit0 or a reason to rerun GPU evidence.
    return {'assets_changed': True, 'CPU_matched_asset_AB': False,
            'entry_count': 6217, 'replacements': 32, 'unchanged_entries': 6185,
            'production_default_enable': False, 'prior_game_acceptance': False,
            'scope': 'Bounded mixed32 resource/visual original-game qualification; not matched CPU A/B, sustained play, whole-game or performance acceptance'}
