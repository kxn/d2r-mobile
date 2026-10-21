"""Exact reviewed848 operation geometry plus alias-gap native-only functional/CPU validation, no new measurements."""
RUNTIME='848f47ccb58db1fa21fdb985e8e1a7f5fdaee0bb'
PREDECESSOR='846fb023c2390358127eb1cedf649bc292b93e4d'
ARTIFACT_BASE='622242282d0868144f1428245a71616fdfeb10b6'
PREDECESSOR_REVIEW='ef2229ac75daceeca3a9d9626f6b1f1844324bae3d5b55da1a0a4a89f8a64967'
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
ROOT_REVIEW='c779a738903a3cfc5411267cf23cdc742cb1f23ebcaf32398400be84e50ad207'
SOURCE_READY='39a2f99ad7b0fdf16f35246aae04bf775447ba524b85ec1e7e57550ed7a7706d'
BASE='6f8188e392f21ff045ef640ad8c4f184de048806'


def validate(p,pinned,read,require):
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair','qualified_predecessor_short'},'candidate gate fields')
    require('sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p and 'performance_tools' in p,
            'short CPU candidate mode excludes longtest, SDP and LLDB')
    require(p['runtime_commit']==RUNTIME and p['driver']['sha256']==DRIVER
            and p['pool_window_bytes']==1048576,'exact848/f830/1MiB target only')
    require(p['phases'][1]['composition_data']['entry_count']==6217,'same6217 only')
    require(c['root_review']['sha256']==ROOT_REVIEW,'exact root candidate review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_EXACT848_CANONICAL_AND_FRESH_RAW28'
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
    require(audit['status']=='EXACT_848F47C_SOURCE_COMPLETE_CANONICAL_FRESH_RAW_CLOSURE_VERIFIED_NO_DEVICE'
            and audit['commit']==RUNTIME and audit['separate_original_NSO_rebuild_and_verify_only_pass'] is True
            and audit['new_host_cache_getter_and_epoch_death_symbols_absent_APP'] is True
            and audit['native_APP_death_env_literal_absent'] is True
            and audit['source_ready']['sha256']==SOURCE_READY
            and audit['operation_geometry_APP_probes_absent'] is True
            and audit['root_source_review']==review['source_review'] and audit['raw_deployment_files']==28
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
            'exact actual846 short root review required')
    previous=read(pinned(c['qualified_predecessor_short']))
    require(previous['status']=='ROOT_REVIEWED_EXACT846_REAL_GAME_SHORT_OFFICIAL_ANALYSIS_CLEANUP_COMPLETE'
            and previous['runtime']==PREDECESSOR and previous['game_modules_executed'] is True
            and previous['route_completed']==3,
            'actual846 functional qualification required')
    for key in ('artifact_review','analysis_ready','analysis'):pinned(previous[key])
    actual=read(pinned(previous['result']))
    require(actual['stage']=='native-original-game-short-candidate-validation-complete'
            and actual['plan']['runtime']==PREDECESSOR
            and actual['original_restored'] is True and actual['cleanup_complete'] is True
            and actual['cleanup_errors']==[] and len(actual['phases'])==1,
            'actual846 short transaction not complete')
    phase=actual['phases'][0]
    require(phase['original_HUD_reached'] is True and phase['game_modules_executed'] is True
            and phase['original_route_completed']==3 and phase['fresh_save_then_stopped'] is True
            and phase['official_performance_complete'] is True and phase['cpu_validity'] is True,
            'actual846 originalgame/CPU/save qualification incomplete')
    # Historical846 is functional/tool qualification, not a matched performance baseline.
    # Never manufacture matched CPU/FPS/heat gains from these independent short runs.
