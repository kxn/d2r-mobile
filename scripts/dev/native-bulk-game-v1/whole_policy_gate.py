"""One explicitly root-authorized d148 shared-whole resource policy probe."""
import copy

AUTH='333d84e3ab71108a19a193ec0d7a3a1b33fca77434ded456092c8f4fc32f46e0'
SCOPE='Bounded shared-whole CPU/memory tradeoff; extra imports/residency and consumer lookup costs; not same-memory CPU AB, net8percent saving, heat/6GB acceptance or default policy.'

def validate(p,pinned,read,require,candidate):
    marker=p['shared_whole_policy_probe']
    require(set(marker)=={'root_authorization'} and marker['root_authorization']['sha256']==AUTH,
            'exact independent whole-policy root authorization required')
    auth=read(pinned(marker['root_authorization']))
    require(auth['status']=='ROOT_AUTHORIZED_BOUNDED_SHARED_WHOLE_POLICY_PROBE'
            and auth['runtime']==p['runtime_commit']==candidate.STORE_RUNTIME
            and p['pool_window_bytes']==auth['pool_window_bytes']==0
            and auth['shared_required'] is True and auth['legacy_fallback_allowed'] is False
            and auth['new_default'] is False and auth['six_GB_acceptance'] is False
            and auth['runtime_or_driver_changes'] is False
            and auth['allow_root_execute_after_tools_source_review'] is True,
            'bounded shared-whole authorization scope differs')
    require(p['canonical']==auth['canonical'] and p['raw']==auth['raw']
            and p['native_candidate_validation']['root_review']==auth['runtime_review']
            and p['driver']['sha256']==auth['driver_sha256']==candidate.DRIVER
            and p['serial']==auth['serial'] and auth['scale_3d']==.60
            and auth['surface']==[2750,1260]
            and p['phases'][1]['composition']['sha256']==auth['native_composition_sha256']
            ==candidate.COLD_NATIVE_COMPOSITION,
            'whole-policy exact runtime/resources/provider differ')
    require(p.get('official_l1i_record') is True and p.get('official_memory_map')==
            {'tool':'/system/bin/showmap','format':'json'},
            'whole-policy requires original official CPU/raw windows and memory snapshot')
    control=read(pinned(auth['actual_1MiB_control']))
    require(control['plan']['runtime']==candidate.STORE_RUNTIME
            and control['plan']['pool_window_bytes']==1048576
            and control['original_restored'] is True and control['cleanup_complete'] is True
            and control['cleanup_errors']==[] and len(control['phases'])==1,
            'actual same-runtime1MiB control not complete/restored')
    phase=control['phases'][0]
    require(phase['original_route_completed']==3 and phase['fresh_save_then_stopped'] is True
            and phase['game_modules_executed'] is True and phase['official_performance_complete'] is True,
            'actual1MiB control route/tools/save absent')
    helper=p['performance_tools']['cpu_helper']
    require(helper['sha256'] in [h for f,h in control['plan']['controller_sources'].items()
                                if f.endswith('/capture-official-cpu.py')],
            'whole-policy official helper differs from actual control')
    # Authenticate the original finite qualification as a prerequisite only.
    # The executed zero policy stays explicit and never enters its exact gate.
    prerequisite=copy.deepcopy(p)
    prerequisite.pop('shared_whole_policy_probe')
    prerequisite['pool_window_bytes']=1048576
    candidate.validate(prerequisite,pinned,read,require)
    p['shared_whole_policy_scope']={'authorization':marker['root_authorization'],
        'actual_1MiB_control':auth['actual_1MiB_control'],'scope':SCOPE,
        'executed_pool_window_bytes':0,'finite_gate_used_as_prerequisite_only':True,
        'new_default':False,'CPU_same_memory_AB':False}
