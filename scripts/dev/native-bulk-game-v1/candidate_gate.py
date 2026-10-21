"""Exact reviewed8480 immutable operation source identity reuse native-only functional/CPU validation, no new measurements."""
RUNTIME='8480a5329ab71d1431f32c159a0def0513f57f3c'
PREDECESSOR='ebe0d0e5512f6d3e816dc0ee164734152012b003'
ARTIFACT_BASE='27fe6da7b4d558ee692a3c30b50a18715997611a'
PREDECESSOR_REVIEW='881be7737eefdfd544509fb82d803ddd84175c4b02fa9afaa7b81d9421bdc2eb'
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
ROOT_REVIEW='35ae09c6f43d2a7857d71f6cfb6dfa66c6fa3f908a3b72b2738b342f1dbaba4d'
SOURCE_READY='0af59a7d79f3a0226020b816d952e07a7529a76879c6334fd3966784c0f4ca8d'
BASE='6f8188e392f21ff045ef640ad8c4f184de048806'


def validate_8480(p,pinned,read,require):
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair','qualified_predecessor_short'},'candidate gate fields')
    require('sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p and 'performance_tools' in p,
            'short CPU candidate mode excludes longtest, SDP and LLDB')
    require(p['runtime_commit']==RUNTIME and p['driver']['sha256']==DRIVER
            and p['pool_window_bytes']==1048576,'exact8480/f830/1MiB target only')
    require(p['phases'][1]['composition_data']['entry_count']==6217,'same6217 only')
    require(len(ROOT_REVIEW)==64,'candidate actual root artifact review remains pending')
    require(c['root_review']['sha256']==ROOT_REVIEW,'exact root candidate review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_EXACT_8480_CANONICAL_AND_FRESH_RAW28'
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
    require(audit['status']=='EXACT_8480A53_SOURCE_COMPLETE_CANONICAL_FRESH_RAW_CLOSURE_VERIFIED_NO_DEVICE'
            and audit['commit']==RUNTIME and audit['separate_original_NSO_rebuild_and_verify_only_pass'] is True
            and audit['new_host_cache_getter_and_epoch_death_symbols_absent_APP'] is True
            and audit['native_APP_death_env_literal_absent'] is True
            and audit['source_ready']['sha256']==SOURCE_READY
            and audit['operation_geometry_APP_probes_absent'] is True
            and audit['texture_retirement_index_APP_probes_absent'] is True
            and audit['operation_image_source_APP_probes_absent'] is True
            and audit['pool_span_search_APP_probes_absent'] is True
            and audit['source_refresh_functional_observer_absent_APP'] is True
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
            'exact actualebe short root review required')
    previous=read(pinned(c['qualified_predecessor_short']))
    require(previous['status']=='ROOT_REVIEWED_EXACT_EBE_REAL_GAME_SHORT_OFFICIAL_ANALYSIS_CLEANUP_COMPLETE'
            and previous['runtime']==PREDECESSOR and previous['game_modules_executed'] is True
            and previous['route_completed']==3 and previous['visual_root_actually_viewed'] is True
            and previous['cleanup_followup_all_owned_not_executing'] is True,
            'actualebe functional qualification required')
    for key in ('artifact_review','analysis_ready','analysis'):pinned(previous[key])
    actual=read(pinned(previous['result']))
    require(actual['stage']=='native-original-game-short-candidate-validation-complete'
            and actual['plan']['runtime']==PREDECESSOR
            and actual['original_restored'] is True and actual['cleanup_complete'] is True
            and actual['cleanup_errors']==[] and len(actual['phases'])==1,
            'actualebe short transaction not complete')
    phase=actual['phases'][0]
    require(phase['original_HUD_reached'] is True and phase['game_modules_executed'] is True
            and phase['original_route_completed']==3 and phase['fresh_save_then_stopped'] is True
            and phase['official_performance_complete'] is True and phase['cpu_validity'] is True,
            'actualebe originalgame/CPU/save qualification incomplete')
    # Actualebe is functional/tool qualification, not a matched CPU/FPS/heat baseline.


COMBINED_RUNTIME='9592893578e56e11c463445592d1e10f1f2e7c5c'
COMBINED_REVIEW='447e9699ac286b0e67a2d789addd6ae08ffda946a4aa1f44a0459b0e9d8468e1'
COMBINED_RAW_PRODUCER='b16184c232b540b92e843be00137b262ae36841d'
COMBINED_RAW='5cbf65d16d01aca261f90691d6a72549ecae737139e3a8487dc2f26da4284700'
COMBINED_CANONICAL='7543b6cf98ed70c5cc41ce959795f85cbe4ccd9abc341408c879a2780ce7a248'


def validate_959(p,pinned,read,require):
    if p['runtime_commit']!=COMBINED_RUNTIME:
        return validate_8480(p,pinned,read,require)
    import copy
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair','qualified_predecessor_short'},'combined gate fields')
    require(c['root_review']['sha256']==COMBINED_REVIEW,'exact959 root review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_959_CANONICAL_AND_VERIFIED_RAW_CACHE28'
            and review['runtime']==COMBINED_RUNTIME and review['tool_ready'] is True
            and review['actual_device_evidence'] is False,'combined source review differs')
    require(p['canonical']['sha256']==COMBINED_CANONICAL and p['raw']['sha256']==COMBINED_RAW
            and review['canonical_ready']==p['canonical'] and review['raw_ready']==p['raw'],
            'combined canonical/raw differs from actual root review')
    canonical=read(pinned(p['canonical']));raw=read(pinned(p['raw']))
    require(canonical['commit']==COMBINED_RUNTIME
            and review['APK']==canonical['canonical_APK']['archive']
            and review['host_ELF_SHA256']==canonical['canonical_native_library_sha256']['libdmhost_android.so']
            and review['host_ELF_BuildID']==canonical['canonical_host_ELF_build_id'],
            'combined actual APK identity differs')
    for key in ('same_rewriter_rules_current_source_verified','raw_provenance_not_relabelled',
                'raw28_hashes_verified','six_APK_libraries_verified','new_fixture_literals_absent_APP'):
        require(review[key] is True,'combined root closure absent '+key)
    pinned(review['source_ready']);pinned(review['APK'])
    require(raw['guarded_runtime_commit']==COMBINED_RUNTIME and raw['raw_producer_commit']==COMBINED_RAW_PRODUCER
            and review['raw_producer_commit']==COMBINED_RAW_PRODUCER
            and raw['all_28_before_after_legal_cache_and_separate_verify_exact'] is True
            and raw['all_23_module_rule_artifact_bytes_equal8480'] is True
            and raw['source_revision_provenance_preserved_not_relabelled'] is True
            and len(raw['source_and_rule_closure'])==3,'reviewed legal raw cache closure differs')
    for row in raw['source_and_rule_closure']:
        require(row['same_current_guarded_source'] is True,'raw rewriter/rules compatibility differs')
        pinned(row['source']);pinned(row['archive'])
    # Keep the full previous asset/provider/qualification gate. Only the host
    # and its honestly recorded compatible raw bundle are different; no old
    # proof is relabelled as a run of the new runtime.
    old_root={'path':'/home/dl/d2r-mobile/.local/logs/operation-source-identity-reuse-8480a53-root-canonical-raw-review.json',
              'sha256':ROOT_REVIEW}
    old=read(pinned(old_root));control=copy.deepcopy(p)
    control['runtime_commit']=RUNTIME;control['canonical']=old['canonical_ready'];control['raw']=old['raw_ready']
    control['native_candidate_validation']['root_review']=old_root
    validate_8480(control,pinned,read,require)


STORE_RUNTIME='d14808f591b0de60c241340c3f5c5df708f4e36b'
STORE_REVIEW='4ee5066775e7578cc663fe2a208e9114160bdaf2fe01c043218b61fc315a3aa8'
STORE_CANONICAL='1b69f1455e775f42eb95833bb21ad4fbd3c35d059f791abbafbb1939dbf4c09f'
STORE_RAW='225361b17da1deadc9f5b14d41a3c61c24b1ec41655b1b7fc54d02cdf053ca9c'


def validate(p,pinned,read,require):
    if p['runtime_commit']!=STORE_RUNTIME:
        return validate_959(p,pinned,read,require)
    import copy
    c=p['native_candidate_validation']
    require(c['root_review']['sha256']==STORE_REVIEW,'exactd148 root review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_D148_CANONICAL_AND_FRESH_RAW28'
            and review['runtime']==STORE_RUNTIME and review['base']==COMBINED_RUNTIME
            and review['tool_ready'] is True and review['actual_device_evidence'] is False,
            'store fusion root source review differs')
    require(p['canonical']['sha256']==STORE_CANONICAL and p['raw']['sha256']==STORE_RAW
            and review['canonical_ready']==p['canonical'] and review['raw_ready']==p['raw'],
            'store fusion canonical/raw differs')
    canonical=read(pinned(p['canonical']));raw=read(pinned(p['raw']))
    require(canonical['commit']==raw['source_commit']==STORE_RUNTIME
            and review['APK']==canonical['canonical_APK']['archive']
            and review['host_ELF_SHA256']==canonical['canonical_native_library_sha256']['libdmhost_android.so']
            and review['host_ELF_BuildID']==canonical['canonical_host_ELF_build_id']
            and raw['all_23_module_rule_artifact_bytes_equal959'] is True
            and raw['executables_equal959'] is True and raw['revision_provenance_fresh'] is True,
            'store fusion actual APK/raw revision differs')
    for key in ('raw28_hashes_verified','six_APK_libraries_verified','new_fixture_literals_absent_APP'):
        require(review[key] is True,'store root closure absent '+key)
    pinned(review['source_ready']);pinned(review['APK'])
    previous=read(pinned({'path':'/home/dl/d2r-mobile/.local/logs/native-cpu-combined-pair-v1-candidate-B-root-actual-review.json',
                          'sha256':'ac39859b16651d3c9aa460f0938e1c508291083be605278bb4c2eaeb2f05b7c8'}))
    require(previous['runtime']==COMBINED_RUNTIME and previous['game_modules_executed'] is True
            and previous['route_completed']==3 and previous['fresh_save_then_stopped'] is True
            and previous['cleanup_followup_all_owned_not_executing'] is True,
            'actual959 prerequisite incomplete')
    actual=read(pinned(previous['result']))
    require(actual['original_restored'] and actual['cleanup_complete'] and actual['cleanup_errors']==[],
            'actual959 original restoration incomplete')
    old_root={'path':'/home/dl/d2r-mobile/.local/logs/cpu-binding-phase-combined-9592893-root-canonical-raw-review.json',
              'sha256':COMBINED_REVIEW}
    old=read(pinned(old_root));control=copy.deepcopy(p)
    control['runtime_commit']=COMBINED_RUNTIME;control['canonical']=old['canonical_ready'];control['raw']=old['raw_ready']
    control['native_candidate_validation']['root_review']=old_root
    validate_959(control,pinned,read,require)
