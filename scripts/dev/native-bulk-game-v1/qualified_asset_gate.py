"""Bounded original-game experiment for the reviewed32 ASTC6 replacements only.

This authenticates existing composer, packer and real-NVN evidence. It does not
change their historical deploy_allowed/game_acceptance flags or encode assets.
"""
from pathlib import Path
import struct

RUNTIME = 'd14808f591b0de60c241340c3f5c5df708f4e36b'
COMBINED_RUNTIME = '179c82dfa412153b6564974e29fc35036e0b3507'
COMBINED_REVIEW = 'c7486cae980b5872811f5706cd4a977d30fd7e6fcd555234ae0be6630e3a2395'
ASSET_REVIEW = 'e2cb6ad4fcc8b45eb379aac8e7ce4236abcbfe57de17d547e8c805ad01a8ef1c'
DUAL_REVIEW = '9a510e8ffcb211c7c4227a69fffb7661a20c48c939ecbcc2cd8e071e32982c1b'
COMPOSITION = '8676e748175eecbc3bdcfc42038e428775cc5917c3293eec56bc5335f55806f3'
TABLE = '030ba91c9aecbecf4a2c335cb7da168b8753e24681a02d9a5466b6a7af5c5b77'
SOURCE_READY = '57a49fa74674961c6352dfa83352489c204ae0f0f3ecd6364ff99ad7ed04b4e3'
OLD_ROOT = Path('/home/dl/d2r-mobile/.local/build/turnip-native-bulk-6217-f830-v1')
OLD_PACK = '8f9562ee202431da15e112577d50762d17db384577cfafdefe8f9930ab1f423e'
OLD_TABLE = '90ac8aa32456fb3be44a478e38f21f84f33cf5345c5a08ac6308b3453ec76113'
NEW_PACK = '395107559ddf96fa8a95573e0b525fedc8256d7475152095f54f8167d83fe7c0'
# Full asset values are filled only after the actual frozen composition and
# root asset review exist. These pending values cannot authorize a device run.
FULL_ASSET_REVIEW = 'c10b5a4977d46fc3c946c739f344dfc5167f52a7f6d15f494de303ad12bf5713'
FULL_SOURCE_READY = '2a717db19222b416e6b91d09f1516da1314c5d08df4664621bc8bbc852206e5b'
FULL_COMPOSITION = 'ffd05d83cba41506e41a374b92372a52b8ad14f7385a4ca50fa3844ffd8fdfe4'
FULL_TABLE = '550b1a9460e28b9528d1b85254ad30590350adb96940e01d6d8827e42fb761c1'
FULL_PACK = 'da4de31e2c5e00c81bed1fd98d6189aed0fb52d95efbbad301f9c7832d7ae3c9'
FULL_ACTUAL_RESULT = '5441c5cc83b6e1a91421bacaef2ea52f3ddd871cc238eebeab1e0a5fd4623335'
FULL_ACTUAL_INDEX = '9c72984483d6ad61fb2895ef80d8fb9667599ece677f339d05b4671b32f19948'
FULL_ACTUAL_REVIEW = 'c774bf4c388ea651e216da7d46fc341a34c18941c226fd382423c42127e97d3a'


def asset_profile(review_sha, require):
    if review_sha == ASSET_REVIEW:
        return dict(count=32, unchanged=6185, cache_unchanged=31040, outputs=12447,
                    source=SOURCE_READY, composition=COMPOSITION, table=TABLE, pack=NEW_PACK,
                    status='ROOT_REVIEWED_ALLOW_BOUNDED_MIXED32_ASTC6_GAME_QUALIFICATION', full=False)
    require(len(FULL_ASSET_REVIEW) == 64 and review_sha == FULL_ASSET_REVIEW,
            'exact root full2838 asset review remains pending or differs')
    return dict(count=2838, unchanged=3379, cache_unchanged=28234, outputs=12448,
                source=FULL_SOURCE_READY, composition=FULL_COMPOSITION, table=FULL_TABLE, pack=FULL_PACK,
                status='ROOT_REVIEWED_ALLOW_BOUNDED_FULL2838_ASTC6_GAME_QUALIFICATION', full=True)



def validate_membership(old, new, old_table, new_table, changed, require, expected_count=32):
    """Compare original strict-packer outputs; no new layout interpretation."""
    expected = {r['id']: r for r in changed}
    require(len(expected) == len(changed) == expected_count, 'exact reviewed replacements required')
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
                    after['image_size'] == row['new_native_bytes'] and after['image_size'] <= before['image_size'] and
                    (old_format, new_format) in ((157, 165), (158, 166)),
                    'replacement identity/size/qualified format differs')


def validate(p, pinned, read, require, compiler_runtime=None):
    mode = p['qualified_native_asset_validation']
    require(set(mode) == {'root_asset_review', 'dual_NVN_review'} and
            p.get('native_candidate_validation') and
            (p['runtime_commit'] in (RUNTIME, COMBINED_RUNTIME) or
             compiler_runtime == p['runtime_commit']) and
            'sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p, 'exact d148 bounded asset experiment only')
    if compiler_runtime == p['runtime_commit']:
        require(mode['root_asset_review']['sha256'] == FULL_ASSET_REVIEW,
                'compiler experiment retains exact full2838 assets')
    if p['runtime_commit'] == COMBINED_RUNTIME:
        require(len(COMBINED_REVIEW) == 64 and
                p['native_candidate_validation']['root_review']['sha256'] == COMBINED_REVIEW and
                mode['root_asset_review']['sha256'] == FULL_ASSET_REVIEW,
                'exact combined runtime review and independent full2838 assets required')
    profile = asset_profile(mode['root_asset_review']['sha256'], require)
    require(mode['dual_NVN_review']['sha256'] == DUAL_REVIEW,
            'actual root asset and dual-format reviews required')
    review = read(pinned(mode['root_asset_review']))
    require(review['status'] == profile['status']
            and review['allow_bounded_original_game_qualification'] is True and
            review['game_acceptance'] is False and review['production_default_enable'] is False and
            review['assets_changed'] is True and review['CPU_matched_asset_AB'] is False and
            review['performance_claim'] is False and review['historical_receipts_unchanged'] is True and
            review['entry_count'] == 6217 and review['replacement_count'] == profile['count'] and
            review['unchanged_entries_exact'] == profile['unchanged'] and
            review['cache_unchanged_entries_exact'] == profile['cache_unchanged'],
            'root bounded asset authorization differs')
    require(review['dual_NVN_actual_review'] == mode['dual_NVN_review'] and
            review['source_ready']['sha256'] == profile['source'] and
            review['composition'] == p['phases'][1]['composition'] and
            review['composition']['sha256'] == profile['composition'] and
            p['phases'][1]['native_table']['sha256'] == review['native_table_sha256'] == profile['table'],
            'mixed composition/table/provenance differs')
    pinned(review['source_pin_verification']); pinned(review['output_pin_verification'])
    source = read(pinned(review['source_ready']))
    for key in ('prepared_manifest', 'actual_complete', 'actual_receipt_index'):
        pinned(source[key])
    if profile['full']:
        require(source['actual_complete']['sha256'] == FULL_ACTUAL_RESULT and
                source['actual_receipt_index']['sha256'] == FULL_ACTUAL_INDEX and
                source['root_actual_producer_review']['sha256'] == FULL_ACTUAL_REVIEW,
                'full2838 actual source receipts differ')
        pinned(source['root_actual_producer_review'])
        pinned(source['validated_all_source_pins']); pinned(source['family_cache_preservation'])
    composition = p['phases'][1]['composition_data']
    require(composition['deploy_allowed'] is False and composition['game_acceptance'] is False and
            composition['replaced_entries'] == profile['count'] and composition['unchanged_native_entries'] == profile['unchanged'] and
            composition['unchanged_cache_entries'] == profile['cache_unchanged'] and
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
    require(outputs['complete'] is True and len(outputs['files']) == profile['outputs'] and
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
    new_pack = read(pinned({'path': str(root / 'native-pack/native-pack.json'), 'sha256': profile['pack']}))
    old_pack = read(pinned({'path': str(OLD_ROOT / 'native-pack/native-pack.json'), 'sha256': OLD_PACK}))
    old_table = pinned({'path': str(OLD_ROOT / 'native-overlay/files/native-layouts.bin'), 'sha256': OLD_TABLE})
    validate_membership(old_pack['entries'], new_pack['entries'], old_table.read_bytes(),
                        pinned(p['phases'][1]['native_table']).read_bytes(), review['changed'], require, profile['count'])
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
            'entry_count': 6217, 'replacements': profile['count'], 'unchanged_entries': profile['unchanged'],
            'production_default_enable': False, 'prior_game_acceptance': False,
            'scope': f"Bounded mixed{profile['count']} resource/visual original-game qualification; not matched CPU A/B, sustained play, whole-game or performance acceptance"}
