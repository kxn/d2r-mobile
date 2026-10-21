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


def validate_store(p,pinned,read,require):
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


# New runtime qualification is independent of the full2838 asset review.
POOL_RUNTIME='179c82dfa412153b6564974e29fc35036e0b3507'
POOL_REVIEW='c7486cae980b5872811f5706cd4a977d30fd7e6fcd555234ae0be6630e3a2395'
POOL_CANONICAL='51206ac64c1819ad759caf2e8c5e6bbc1f5a85a98d8596ea815ecf05778bb89c'
POOL_RAW='842ac52d3902c8173151af0f07bb55ea52760080e3bf07cf584225f28dc78af2'
POOL_SOURCE='cc87e79cbe7cf5573980ab6d8d5a902add232c82cbf3987a478290b2f3acba63'
POOL_ASSETS='c10b5a4977d46fc3c946c739f344dfc5167f52a7f6d15f494de303ad12bf5713'


def reviewed_artifacts(p, review, pinned, read, require):
    """Existing canonical/raw identities, APP closure and default runtime flags."""
    for key in ('canonical', 'raw'):
        actual=review[key+'_ready']
        require(actual['sha256']==p[key]['sha256'] and str(pinned(actual))==str(pinned(p[key])),
                'canonical/raw differ from root-reviewed records')
    canonical=read(pinned(p['canonical']));raw=read(pinned(p['raw']))
    require(canonical['commit']==raw['source_commit']==p['runtime_commit']
            and review['APK']==canonical['canonical_APK']['archive']
            and review['host_ELF_SHA256']==canonical['canonical_native_library_sha256']['libdmhost_android.so']
            and review['host_ELF_BuildID']==canonical['canonical_host_ELF_build_id']
            and len(canonical['canonical_native_library_sha256'])==6
            and len(raw['28_deployment_file_hashes'])==28 and len(raw['raw_original_inputs'])==3
            and raw['revision_provenance_fresh'] is True, 'reviewed APK/raw provenance differs')
    flags=canonical['effective_build']
    source=canonical['source_only_candidate']
    require(flags['cold_phase_compile_reference'] is False and flags['cold_phase_default_outline'] is True
            and flags['pool_pin_compile_reference'] is False and flags['pool_pin_default_borrow'] is True
            and 'vk_test_pool_operation_pin_counts' in canonical['new_APP_fixture_exclusion']['symbols_absent']
            and review['source_ready']['sha256']==source['sha256']
            and str(pinned(review['source_ready']))==str(pinned(source))
            and pinned(source).stat().st_size==source['bytes'], 'effective flags/APP closure/sourcechain differs')
    require(review['actual_APP_preprocessed_units']==6, 'six actual APP closures required')
    for key in ('raw28_hashes_verified','six_APK_libraries_verified','new_fixture_symbols_absent_APP','compile_reference_OFF'):
        require(review[key] is True,'root closure absent '+key)
    pinned(review['APK'])
    return canonical,raw


def validate(p,pinned,read,require):
    if 'native_host_experiment' in p:
        require('native_compiler_experiment' not in p, 'choose one reviewed experiment kind')
        return validate_compiler(p,pinned,read,require,resource_preparation=True)
    if 'native_compiler_experiment' in p:
        return validate_compiler(p,pinned,read,require)
    if p['runtime_commit'] != POOL_RUNTIME:
        return validate_store(p,pinned,read,require)
    import copy
    c=p['native_candidate_validation']
    require(set(c)=={'root_review','qualified_base_pair','qualified_predecessor_short'},
            'exact pool combined gate fields')
    require(len(POOL_REVIEW)==64 and c['root_review']['sha256']==POOL_REVIEW,
            'exact179c82d actual root artifact review remains pending or differs')
    require(p['pool_window_bytes']==1048576 and p['driver']['sha256']==DRIVER
            and p.get('official_l1i_record') is True and p.get('performance_tools'),
            'pool combined exact f830/1MiB only')
    require(p.get('qualified_native_asset_validation',{}).get('root_asset_review',{}).get('sha256')==POOL_ASSETS,
            'pool combined requires independent full2838 asset review')
    review=read(pinned(c['root_review']))
    require(review['status']=='ROOT_REVIEWED_179C82D_CANONICAL_AND_FRESH_RAW28'
            and review['runtime']==POOL_RUNTIME
            and review['base']=='53c7d26628142a383c9c5f8d39a3edc23a1674c3'
            and review['tool_ready'] is True and review['actual_device_evidence'] is False,
            'pool combined source review differs')
    require(p['canonical']['sha256']==POOL_CANONICAL and p['raw']['sha256']==POOL_RAW
            and review['canonical_ready']==p['canonical'] and review['raw_ready']==p['raw'],
            'pool combined reviewed canonical/raw differs')
    canonical,raw=reviewed_artifacts(p,review,pinned,read,require)
    require(raw['all_23_module_rule_artifact_bytes_equal_53c'] is True
            and raw['executables_equal_53c'] is True
            and canonical['source_only_candidate']['sha256']==POOL_SOURCE,
            'pool combined raw compatibility/source differs')
    require(len(review['component_source_reviews'])==2, 'pool combined component root reviews required')
    for row in review['component_source_reviews']:pinned(row)
    full=read(pinned({'path':'/home/dl/d2r-mobile/.local/logs/native-qualified-astc6-full-game-d148-root-actual-review.json',
                      'sha256':'446ba9c8f18d8f94cc4d8a680e75a3531bb0582e3ebe24d32e9c6fc3ae767779'}))
    require(full['status']=='ROOT_REVIEWED_ACTUAL_FULL2838_ORIGINAL_GAME_SHORT_QUALIFICATION'
            and full['game_modules_executed'] is True and full['original_three_routes_complete'] is True
            and full['fresh_save2_then_stopped'] is True and full['original_restored'] is True
            and full['cleanup_complete'] is True and full['cleanup_errors']==[]
            and full['visual_root_actually_viewed_before_after'] is True,
            'actual full2838 d148 resource/game/restore prerequisite incomplete')
    for key in ('facts','result','source'):pinned(full[key])
    # Original runtime/provider prerequisites remain independently authenticated.
    # This is a surrogate prerequisite check, never a run or source relabelling.
    old_root={'path':'/home/dl/d2r-mobile/.local/logs/cpu-store-phase-combined-d14808f-root-canonical-raw-review.json',
              'sha256':STORE_REVIEW}
    old=read(pinned(old_root));control=copy.deepcopy(p)
    control['runtime_commit']=STORE_RUNTIME;control['canonical']=old['canonical_ready'];control['raw']=old['raw_ready']
    control['native_candidate_validation']['root_review']=old_root
    validate_store(control,pinned,read,require)


# Fixed, already game-qualified baseline; compiler experiments do not replay the
# historical 8480/959 surrogate runtime chain or relabel those runs.
POOL_ACTUAL={'path':'/home/dl/d2r-mobile/.local/logs/native-cpu-pool-pin-coherent-179-full2838-root-actual-review.json',
             'sha256':'24f3acbe1ca3899592af629d6ca04324de7230f645f3dbc6246db47f0b7a37ff'}
THIN_COMPILE=['-flto=thin']
THIN_LINK=['-flto=thin','-O3','-fuse-ld=lld','-Wl,--thinlto-jobs=2']


def validate_compiler(p,pinned,read,require,resource_preparation=False):
    """Reviewed source successor, retaining original179 build/assets/raw reference.

    The caller authenticates the complete inputs SHA, including root_review;
    source equivalence is the root's independent diff finding, not self-approval.
    """
    mode = ('native_host_experiment' if resource_preparation else 'native_compiler_experiment')
    source_base = p[mode].get('base_runtime') if resource_preparation else POOL_RUNTIME
    expected = ({'kind':'resource-preparation','base_runtime':source_base} if resource_preparation
                else {'strategy':'thinlto','base_runtime':POOL_RUNTIME})
    require(p[mode]==expected
            and set(p['native_candidate_validation'])=={'root_review'}
            and source_base and p['runtime_commit']!=source_base, 'reviewed source parent / experiment mode differs')
    require(p['pool_window_bytes']==1048576 and p['driver']['sha256']==DRIVER
            and (p.get('official_l1i_record') is True or bool(p.get('official_cpu_aa')) or bool(p.get('official_api_frame')))
            and (p.get('performance_tools') or bool(p.get('official_api_frame')))
            and p.get('qualified_native_asset_validation',{}).get('root_asset_review',{}).get('sha256')==POOL_ASSETS,
            'compiler experiment requires original f830/1MiB/full2838 and official tools')
    review=read(pinned(p['native_candidate_validation']['root_review']))
    status = ('ROOT_REVIEWED_HOST_RESOURCE_PREPARATION_CANONICAL_AND_FRESH_RAW28' if resource_preparation
              else 'ROOT_REVIEWED_HOST_THINLTO_CANONICAL_AND_FRESH_RAW28')
    require(review['status']==status
            and review['runtime']==p['runtime_commit'] and review['base']==source_base
            and review['tool_ready'] is True and review['actual_device_evidence'] is False,
            'independently reviewed host experiment required')
    if resource_preparation:
        require(review['host_only_changes'] is True and review['game_logic_unchanged'] is True,
                'independently reviewed host-only preparation source diff required')
    else:
        require(review['compiler_only_changes'] is True and review['runtime_C_CPP_source_equal_base'] is True,
                'independently reviewed compiler-only source diff required')
    canonical,raw=reviewed_artifacts(p,review,pinned,read,require)
    source=read(pinned(review['source_ready']))
    require(source['commit']==p['runtime_commit'] and source['base']==source_base,
            'compiler source-ready revision/base differs')
    base_review=read(pinned({'path':'/home/dl/d2r-mobile/.local/logs/cpu-pool-pin-coherent-179c82d-root-canonical-raw-review.json',
                            'sha256':POOL_REVIEW}))
    base=read(pinned(base_review['canonical_ready']));base_raw=read(pinned(base_review['raw_ready']))
    require(base_review['canonical_ready']['sha256']==POOL_CANONICAL and base_review['raw_ready']['sha256']==POOL_RAW
            and raw['raw_original_inputs']==base_raw['raw_original_inputs'], 'original NSO inputs differ from179')
    stable={k:v for k,v in base_raw['28_deployment_file_hashes'].items()
            if not k.endswith('.provenance.json') and k not in ('build_src.sha256','rewrite-manifest.json')}
    require(len(stable)==23 and all(raw['28_deployment_file_hashes'].get(k)==v for k,v in stable.items()),
            'compiler experiment changed rewritten modules/rules')
    for kind in ('symbols_absent','literals_absent'):
        require(set(base['new_APP_fixture_exclusion'][kind])<=set(canonical['new_APP_fixture_exclusion'][kind]),
                'compiler experiment lost APP fixture exclusions')
    require(len(canonical['new_APP_fixture_exclusion']['actual_preprocessed_closure'])==6,
            'compiler experiment requires six actual APP units')
    flags=canonical['effective_build'];manifest=read(pinned(canonical['canonical_build_manifest']))
    for key in ('tlsdesc','shared_pools_compiled_default_OFF','shared_import_windows_compiled',
                'native_resources_compiled','opt_level','NDK','API','ABI','jobs'):
        require(flags[key]==base['effective_build'][key], 'compiler experiment changed runtime build option '+key)
    require(manifest['source_revision']==p['runtime_commit']
            and manifest['native_library_sha256']==canonical['canonical_native_library_sha256']
            and manifest['apk_sha256']==canonical['canonical_APK']['archive']['sha256']
            and manifest['host_opt_level']=='3', 'canonical effective manifest differs')
    if resource_preparation:
        require(flags.get('host_compiler_strategy') in (None,'original-O3')
                and flags.get('thinlto',False) is False and not flags.get('thinlto_compile_options')
                and not flags.get('thinlto_link_options') and manifest.get('host_thinlto',False) is False
                and not manifest.get('host_extra_compile_options') and not manifest.get('host_extra_link_options'),
                'host preparation retains original179 O3 compiler policy without LTO or extra flags')
    else:
        require(flags['host_compiler_strategy']=='thinlto' and flags['thinlto'] is True
            and flags['thinlto_backend_jobs']==2 and flags['thinlto_compile_options']==THIN_COMPILE
            and flags['thinlto_link_options']==THIN_LINK
            and manifest['host_thinlto'] is True
            and manifest['host_thinlto_backend_jobs']==2
            and manifest['host_extra_compile_options']==THIN_COMPILE and manifest['host_extra_link_options']==THIN_LINK,
            'canonical ThinLTO effective manifest/flags differ')
    actual=read(pinned(POOL_ACTUAL))
    require(actual['runtime']==POOL_RUNTIME and actual['canonical_raw_source_review']['sha256']==POOL_REVIEW
            and all(actual[k] is True for k in ('game_modules_executed','original_three_routes_complete',
                'fresh_save2_then_stopped','original_restored','cleanup_complete','visual_root_actually_viewed_after'))
            and actual['cleanup_errors']==[], 'actual179 full2838 game/restore prerequisite incomplete')
    for key in ('original_report','actual_output_archive_and_original_path_map','phase','save'):pinned(actual[key])
    # Only this successful gate can extend the otherwise exact runtime asset gate.
    return p['runtime_commit']
