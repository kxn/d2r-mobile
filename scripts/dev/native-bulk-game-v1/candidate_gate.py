"""Exact reviewed846 logical descriptor native-only functional/CPU validation, no new measurements."""
RUNTIME='846fb023c2390358127eb1cedf649bc292b93e4d'
PREDECESSOR='633ea750561f3cc3b072acc82e44a444cb25b045'
ARTIFACT_BASE='2ad92a2e6cca8679a177de343183697211ce5584'
PREDECESSOR_REVIEW='11732c5e0abee431d49d86e9467bb4ac70e794f74f816bf2c8bf8e61e5213798'
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
ROOT_REVIEW='10ca45052e98b6cd23e69435b871229918d0dfa0b0ccd467889275061fd074a2'
BASE='6f8188e392f21ff045ef640ad8c4f184de048806'


def validate(p,pinned,read,require):
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair','qualified_predecessor_short'},'candidate gate fields')
    require('sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p and 'performance_tools' in p,
            'short CPU candidate mode excludes longtest, SDP and LLDB')
    require(p['runtime_commit']==RUNTIME and p['driver']['sha256']==DRIVER
            and p['pool_window_bytes']==1048576,'exact846/f830/1MiB target only')
    require(p['phases'][1]['composition_data']['entry_count']==6217,'same6217 only')
    require(c['root_review']['sha256']==ROOT_REVIEW,'exact root candidate review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_EXACT846_CANONICAL_AND_FRESH_RAW28'
            and review['runtime']==RUNTIME and review['base']==ARTIFACT_BASE
            and review['actual_device_authorized'] is False and review['tool_ready_authorized'] is True,
            'candidate source review differs')
    for key in ('canonical','raw'):
        actual=review[key+'_ready']
        require(str(pinned(actual))==str(pinned(p[key])) and actual['sha256']==p[key]['sha256'],
                'canonical/raw not those actually root-reviewed')
    canonical=read(pinned(p['canonical']))
    raw=read(pinned(p['raw']))
    require(len(canonical['canonical_native_library_sha256'])==6
            and review['APK']==canonical['canonical_APK']['archive']
            and review['host_ELF_SHA256']==canonical['canonical_native_library_sha256']['libdmhost_android.so']
            and review['host_ELF_BuildID']==canonical['canonical_host_ELF_build_id']
            and len(raw['raw_original_inputs'])==3 and len(raw['28_deployment_file_hashes'])==28,
            'reviewed APK/libraries/raw original inputs differ')
    pinned(review['APK']); pinned(review['source_review'])
    audit=read(pinned(review['complete_audit']))
    require(audit['status']=='EXACT_846FB02_REVIEWED_LOGICAL_DESCRIPTOR_SOURCE_CANONICAL_FRESH_RAW_CLOSURE_VERIFIED_NO_DEVICE'
            and audit['commit']==RUNTIME and audit['separate_fresh_original_NSO_and_verify_only'] is True
            and audit['actual_APP_dump_stats_observer_absent'] is True
            and audit['source_ready']['sha256']=='92c71a51d5bd2b58ceda9762189dfa12358ee53e82aa224b5292bcc556471afc'
            and audit['root_review']==review['source_review'] and audit['raw_deployment_files']==28
            and audit['canonical_ready']==p['canonical'] and audit['raw_ready']==p['raw']
            and len(audit['six_library_hashes_and_LOAD_alignment'])==6
            and {r['library']:r['sha256'] for r in audit['six_library_hashes_and_LOAD_alignment']}==canonical['canonical_native_library_sha256'],
            'candidate complete actual build/verify closure differs')
    qualified=read(pinned(c['qualified_base_pair']))
    require(qualified['status']=='ACTUAL_ORIGINAL_GAME_PAIRED_FUNCTIONAL_OFFICIAL_CPU_GPU_MEMORY_PASS_CLEANUP_FOLLOWUP_CONFIRMED'
            and qualified['runtime']==BASE and qualified['driver']==DRIVER
            and qualified['visual_root_actually_viewed']
            and qualified['cleanup_followup_all_owned_not_executing'],
            'existing6f truepaired qualification/cleanup required')
    for row in qualified['refs']:pinned(row)

    require(c['qualified_predecessor_short']['sha256']==PREDECESSOR_REVIEW,
            'exact actual633 short root review required')
    previous=read(pinned(c['qualified_predecessor_short']))
    require(previous['status']=='ROOT_REVIEWED_EXACT633_REAL_GAME_SHORT_OFFICIAL_ANALYSIS_CLEANUP_COMPLETE'
            and previous['runtime']==PREDECESSOR and previous['game_modules_executed'] is True
            and previous['route_completed']==3,
            'actual633 functional qualification required')
    for key in ('artifact_review','analysis_ready','analysis'):pinned(previous[key])
    actual=read(pinned(previous['result']))
    require(actual['stage']=='native-original-game-short-candidate-validation-complete'
            and actual['plan']['runtime']==PREDECESSOR
            and actual['original_restored'] is True and actual['cleanup_complete'] is True
            and actual['cleanup_errors']==[] and len(actual['phases'])==1,
            'actual633 short transaction not complete')
    phase=actual['phases'][0]
    require(phase['original_HUD_reached'] is True and phase['game_modules_executed'] is True
            and phase['original_route_completed']==3 and phase['fresh_save_then_stopped'] is True
            and phase['official_performance_complete'] is True and phase['cpu_validity'] is True,
            'actual633 originalgame/CPU/save qualification incomplete')
    # Historical633 is functional/tool qualification, not a matched performance baseline.
    # Never manufacture matched CPU/FPS/heat gains from these independent short runs.
