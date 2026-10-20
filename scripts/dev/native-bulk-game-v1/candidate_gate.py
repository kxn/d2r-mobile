"""Exact reviewed26b native-only functional/CPU validation, no new measurements."""
RUNTIME='26b0764a925b46ec6e901d52bac279a639f54eb3'
DRIVER='f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce'
ROOT_REVIEW='7f95fbf883e19f6f999bd44b46ebbcee6b518fdc828e9090f67c9c9f66716b24'
BASE='6f8188e392f21ff045ef640ad8c4f184de048806'


def validate(p,pinned,read,require):
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair'},'candidate gate fields')
    require('sustained_play' not in p and 'official_gpu_tools' not in p and
            'counter_snapshot' not in p and 'performance_tools' in p,
            'short CPU candidate mode excludes longtest, SDP and LLDB')
    require(p['runtime_commit']==RUNTIME and p['driver']['sha256']==DRIVER
            and p['pool_window_bytes']==1048576,'exact26b/f830/1MiB target only')
    require(p['phases'][1]['composition_data']['entry_count']==6217,'same6217 only')
    require(c['root_review']['sha256']==ROOT_REVIEW,'exact root candidate review required')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_SOURCE_CANONICAL_RAW_REVIEW_COMPLETE_UNIFIED_CANDIDATE_NOT_DEVICE_ACCEPTANCE'
            and review['revision']==RUNTIME,'candidate source review differs')
    require(all(review['actual_artifact_checks'][k] is True for k in
                ['APK_and_six_library_hashes','fresh_raw_bundle28_files','raw_source_revision_exact']),
            'candidate artifacts incomplete')
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
