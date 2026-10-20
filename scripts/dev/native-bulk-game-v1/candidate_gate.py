"""Exact reviewed80a native-only functional/CPU validation, no new measurements."""
RUNTIME='80a71bcf10e92b100027ffa32d71eec85852d7fc'
PREDECESSOR='26b0764a925b46ec6e901d52bac279a639f54eb3'
PREDECESSOR_REVIEW='68f7ba03d533396b6c43798de87a1c94b9d6c539ab4ece23681953b84d4cd210'
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
ROOT_REVIEW='6e824e10cac00a6748abecc725a5ecbf8b726f00fd6273dec6c98d8e48140858'
BASE='6f8188e392f21ff045ef640ad8c4f184de048806'


def validate(p,pinned,read,require):
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair','qualified_predecessor_short'},'candidate gate fields')
    require('sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p and 'performance_tools' in p,
            'short CPU candidate mode excludes longtest, SDP and LLDB')
    require(p['runtime_commit']==RUNTIME and p['driver']['sha256']==DRIVER
            and p['pool_window_bytes']==1048576,'exact80a/f830/1MiB target only')
    require(p['phases'][1]['composition_data']['entry_count']==6217,'same6217 only')
    require(c['root_review']['sha256']==ROOT_REVIEW,'exact root candidate review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_SOURCE_CANONICAL_RAW_CANDIDATE_NO_DEVICE_YET'
            and review['runtime']==RUNTIME and review['base']==PREDECESSOR
            and review['eligible_for_short_candidate_only'] is True,'candidate source review differs')
    require(review['native_libraries_checked']==6 and review['raw_deployment_files_checked']==28
            and review['actual_original_raw_inputs_checked']==3, 'candidate artifacts incomplete')
    refs={str(pinned(row)):row for row in review['refs']}
    for key in ('canonical','raw'):
        path=str(pinned(p[key]))
        require(path in refs and refs[path]['sha256']==p[key]['sha256'],
                'canonical/raw not those actually root-reviewed')
    qualified=read(pinned(c['qualified_base_pair']))
    require(qualified['status']=='ACTUAL_ORIGINAL_GAME_PAIRED_FUNCTIONAL_OFFICIAL_CPU_GPU_MEMORY_PASS_CLEANUP_FOLLOWUP_CONFIRMED'
            and qualified['runtime']==BASE and qualified['driver']==DRIVER
            and qualified['visual_root_actually_viewed']
            and qualified['cleanup_followup_all_owned_not_executing'],
            'existing6f truepaired qualification/cleanup required')
    for row in qualified['refs']:pinned(row)

    require(c['qualified_predecessor_short']['sha256']==PREDECESSOR_REVIEW,
            'exact actual26b short root review required')
    previous=read(pinned(c['qualified_predecessor_short']))
    require(previous['status']=='ROOT_REVIEWED_26B_ORIGINAL_GAME_SHORT_PASS_NOT_MATCHED_PERFORMANCE_OR_HEAT'
            and previous['runtime']==PREDECESSOR and previous['save_commits']==2
            and previous['original_APK_six_dirs_restored'] is True
            and previous['cleanup_errors']==[] and previous['main_integration_eligible'] is True,
            'actual26b functional/restore qualification required')
    evidence=[read(pinned(row)) for row in previous['refs']]
    actual=next((row for row in evidence if row.get('stage')==
                'native-original-game-short-candidate-validation-complete'),None)
    require(actual is not None and actual['plan']['runtime']==PREDECESSOR
            and actual['original_restored'] is True and actual['cleanup_complete'] is True
            and actual['cleanup_errors']==[] and len(actual['phases'])==1,
            'actual26b short transaction not complete')
    phase=actual['phases'][0]
    require(phase['original_HUD_reached'] is True and phase['game_modules_executed'] is True
            and phase['original_route_completed']==3 and phase['fresh_save_then_stopped'] is True
            and phase['official_performance_complete'] is True and phase['cpu_validity'] is True,
            'actual26b originalgame/CPU/save qualification incomplete')
    # Stability was false in actual26b; this is functional qualification only.
    # Never manufacture a matched CPU/FPS/heat baseline from that short run.
