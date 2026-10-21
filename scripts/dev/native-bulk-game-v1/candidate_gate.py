"""Exact reviewed10c@36bb836 native-only functional/CPU validation, no new measurements."""
RUNTIME='10c53698a3155522e743a01f7e5b0e3d860b69ad'
PREDECESSOR='80a71bcf10e92b100027ffa32d71eec85852d7fc'
ARTIFACT_BASE='36bb836affcd1ec62b164602df5f0cabbf0ef22a'
PREDECESSOR_REVIEW='145dbcfd60d0d9aec9d810e727a4b5b68f08c0a6a3ef1bf830fb2466442380a8'
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
ROOT_REVIEW='91e6618783ce50c88d81a00c60a887cf73111bc27af7b91d12548f141b117309'
BASE='6f8188e392f21ff045ef640ad8c4f184de048806'


def validate(p,pinned,read,require):
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair','qualified_predecessor_short'},'candidate gate fields')
    require('sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p and 'performance_tools' in p,
            'short CPU candidate mode excludes longtest, SDP and LLDB')
    require(p['runtime_commit']==RUNTIME and p['driver']['sha256']==DRIVER
            and p['pool_window_bytes']==1048576,'exact10c@36bb836/f830/1MiB target only')
    require(p['phases'][1]['composition_data']['entry_count']==6217,'same6217 only')
    require(c['root_review']['sha256']==ROOT_REVIEW,'exact root candidate review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_EXACT_10C_CANONICAL_AND_FRESH_RAW28'
            and review['runtime']==RUNTIME and review['base']==ARTIFACT_BASE
            and review['device_access'] is False,
            'candidate source review differs')
    require(review['raw_deployment_files']==28, 'candidate raw artifacts incomplete')
    for key in ('canonical','raw'):
        actual=review[key+'_ready']
        require(str(pinned(actual))==str(pinned(p[key])) and actual['sha256']==p[key]['sha256'],
                'canonical/raw not those actually root-reviewed')
    canonical=read(pinned(p['canonical']))
    raw=read(pinned(p['raw']))
    require(len(review['six_native_library_sha256'])==6
            and review['six_native_library_sha256']==canonical['canonical_native_library_sha256']
            and review['APK']==canonical['canonical_APK']['archive']
            and review['host_ELF_sha256']==canonical['canonical_native_library_sha256']['libdmhost_android.so']
            and review['host_ELF_BuildID']==canonical['canonical_host_ELF_build_id']
            and len(raw['raw_original_inputs'])==3 and len(raw['28_deployment_file_hashes'])==28,
            'reviewed APK/libraries/raw original inputs differ')
    pinned(review['APK']); pinned(review['source_review'])
    audit=read(pinned(review['complete_audit']))
    require(audit['status']=='EXACT_10C_SOURCE_CANONICAL_AND_FRESH_RAW_CLOSURE_VERIFIED_NO_DEVICE'
            and audit['commit']==RUNTIME and audit['separate_fresh_original_NSO_and_verify_only'] is True
            and audit['actual_APP_test_observer_absent'] is True and audit['raw_deployment_files']==28
            and audit['canonical_ready']==p['canonical'] and audit['raw_ready']==p['raw'],
            'candidate complete actual build/verify closure differs')
    qualified=read(pinned(c['qualified_base_pair']))
    require(qualified['status']=='ACTUAL_ORIGINAL_GAME_PAIRED_FUNCTIONAL_OFFICIAL_CPU_GPU_MEMORY_PASS_CLEANUP_FOLLOWUP_CONFIRMED'
            and qualified['runtime']==BASE and qualified['driver']==DRIVER
            and qualified['visual_root_actually_viewed']
            and qualified['cleanup_followup_all_owned_not_executing'],
            'existing6f truepaired qualification/cleanup required')
    for row in qualified['refs']:pinned(row)

    require(c['qualified_predecessor_short']['sha256']==PREDECESSOR_REVIEW,
            'exact actual80a short root review required')
    previous=read(pinned(c['qualified_predecessor_short']))
    require(previous['status']=='ROOT_REVIEWED_80A_ORIGINAL_GAME_SHORT_FUNCTIONAL_AND_CAPTURE_PASS'
            and previous['runtime']==PREDECESSOR and previous['save_commits']==2
            and previous['original_APK_six_dirs_restored'] is True
            and previous['cleanup_errors']==[] and previous['integration_eligible_functionally'] is True,
            'actual80a functional/restore qualification required')
    evidence=[read(pinned(row)) for row in previous['refs']]
    actual=next((row for row in evidence if row.get('stage')==
                'native-original-game-short-candidate-validation-complete'),None)
    require(actual is not None and actual['plan']['runtime']==PREDECESSOR
            and actual['original_restored'] is True and actual['cleanup_complete'] is True
            and actual['cleanup_errors']==[] and len(actual['phases'])==1,
            'actual80a short transaction not complete')
    phase=actual['phases'][0]
    require(phase['original_HUD_reached'] is True and phase['game_modules_executed'] is True
            and phase['original_route_completed']==3 and phase['fresh_save_then_stopped'] is True
            and phase['official_performance_complete'] is True and phase['cpu_validity'] is True,
            'actual80a originalgame/CPU/save qualification incomplete')
    # This predecessor is functional/tool qualification, not a matched performance baseline.
    # Never manufacture a matched CPU/FPS/heat baseline from that short run.
