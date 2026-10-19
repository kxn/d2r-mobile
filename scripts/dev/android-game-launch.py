#!/usr/bin/env python3
"""冷启动或接管已有角色，依据当前进程原 UI/玩家回读确认就绪。
Cold-start or attach to an existing character; accept original UI/player
observations from the current process, never stale logs or a launch intent alone.
"""
import argparse
import json
import re
from pathlib import Path
import struct
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', default='JNJF49UGBQ4XSCZT')
parser.add_argument('--attach', action='store_true', help='observe an existing process launched by a capture tool; never restart it')
parser.add_argument('--operation-mode',choices=['docked','handheld'],help='startup-only original operation/render profile; default remains docked; handheld profile stays 720 high with --game-res aspect ratio')
parser.add_argument('--gpu-profile', action='store_true')
opus_mode=parser.add_mutually_exclusive_group()
opus_mode.add_argument('--opus-shadow', action='store_true', help='bounded native Opus correctness mirror; original SDK remains sole audio output (default off)')
opus_mode.add_argument('--native-opus', action=argparse.BooleanOptionalAction, default=None, help='native core math for adapted decoders, retaining original SDK packet validation/output queue (default on; --no-native-opus for SDK comparison)')
parser.add_argument('--performance-hints',action='store_true',help='experimental public Android render-thread workload hints')
parser.add_argument('--storage-usage-reference',action='store_true',help='startup-only comparison: reserve Storage usage from the NVN IMAGE hint instead of first actual binding')
parser.add_argument('--attachment-usage-reference',action='store_true',help='startup-only comparison: reserve Color Attachment usage for every supporting format')
parser.add_argument('--texel-storage',action='store_true',help='experimental paired RGBA32F buffer storage path; requires matching alternative shader sidecars')
present_modes=parser.add_mutually_exclusive_group()
present_modes.add_argument('--shader-present',dest='shader_present',action='store_true',help='force same-format native-size shader presentation; reserves swapchain color attachment usage at startup')
present_modes.add_argument('--reference-present',dest='shader_present',action='store_false',help='use the original Vulkan blit presentation')
parser.set_defaults(shader_present=None)
parser.add_argument('--eager-pool-mirrors-reference',action='store_true',help='startup-only comparison: allocate a Vulkan buffer mirror for every registered NVN pool')
parser.add_argument('--general-depth-layouts-reference',action='store_true',help='startup-only comparison: retain GENERAL for depth/stencil, preserving optimized color layouts')
parser.add_argument('--general-compute-layouts-reference',action='store_true',help='startup-only comparison: retain GENERAL for readonly compute image sampling')
parser.add_argument('--general-sampled-array-layouts-reference',action='store_true',help='comparison: retain GENERAL for sampled-only color arrays; other image layouts remain optimized')
parser.add_argument('--global-buffer-memory-reference',action='store_true',help='comparison: retain global memory dependencies for buffer-only consumers')
parser.add_argument('--writable-depth-attachments-reference',action='store_true',help='startup-only comparison: do not specialize provably readonly depth/stencil attachments')
parser.add_argument('--clear-attachments-reference',action='store_true',help='comparison: retain untouched sibling attachments in clear-only passes')
parser.add_argument('--texture-dirty-reference',action='store_true',help='comparison: recheck texture dirty pages on every binding')
parser.add_argument('--zero-draw-reference',action='store_true',help='comparison: retain upstream outline draws whose inputs and destinations are proven zero')
parser.add_argument('--uniform-specialization',action=argparse.BooleanOptionalAction,default=True,help='exact per-draw Uniform specialization with safe fallback (default on)')
parser.add_argument('--uniform-predicate',action=argparse.BooleanOptionalAction,default=False,help='optional hybrid exact-word plus integer predicate specialization (default off)')
parser.add_argument('--deferred-mip-clears',action=argparse.BooleanOptionalAction,default=False,help='experimental bounded disjoint-mip clear deferral (default off)')
parser.add_argument('--display-refresh-hz',type=float,help='optional Android Surface/display refresh request; preserves native rendering resolution and game speed')
parser.add_argument('--mailbox-present',action='store_true',help='startup-only WSI experiment: request advertised MAILBOX; otherwise FIFO remains active')
parser.add_argument('--dedicated-present-worker',action='store_true',help='startup-only experiment: serialize WSI acquire/present on a worker with a second queue of the same family')
parser.add_argument('--present-worker-shared-queue',action='store_true',help='isolate presentation worker scheduling while preserving the original Vulkan queue; requires --dedicated-present-worker')
parser.add_argument('--descriptor-template-reference',action='store_true',help='comparison: use individual descriptor write descriptions instead of the compiled update template')
parser.add_argument('--descriptor-history-reference',action='store_true',help='comparison: cache only consecutive identical descriptor contents')
parser.add_argument('--descriptor-reuse-reference',action='store_true',help='comparison: reset descriptor pools after each completed submission')
parser.add_argument('--descriptor-batch-reference',action='store_true',help='comparison: allocate immutable descriptor sets one at a time')
parser.add_argument('--dynamic-uniform-reference',action='store_true',help='comparison: retain static UBO descriptor offsets')
parser.add_argument('--dynamic-uniform-narrow-reference',action='store_true',help='comparison: limit dynamic UBO offsets to the original four slots')
parser.add_argument('--pool-lookup-reference',action='store_true',help='comparison: resolve every buffer address under the resource lock and remark all access bitmaps')
parser.add_argument('--upload-scan-reference',action='store_true',help='comparison: use the original scalar changed-word scan for dirty buffer uploads')
parser.add_argument('--texture-binding-reference', action='store_true', help='resolve texture binding metadata through the uncached reference path')
parser.add_argument('--sampler-lookup-reference',action='store_true',help='comparison: lock the sampler registry for every binding')
parser.add_argument('--no-fxaa',action='store_true',help='upstream optional quality tradeoff: retain native resolution but bypass FXAA edge filtering (default off)')
parser.add_argument('--bloom-quarter',action=argparse.BooleanOptionalAction,default=True,help='upstream bloom A+B1 merge with full-chain proof and original C materialization (default on; --no-bloom-quarter for comparison)')
parser.add_argument('--custom-vulkan-driver',action='store_true',help='startup-only: load the verified per-app driver from runtime/vulkan-driver')
layout_policy=parser.add_mutually_exclusive_group()
layout_policy.add_argument('--optimal-image-layouts',action='store_true',default=True,help='precise color/read-only image layouts (default); complex accesses retain GENERAL')
layout_policy.add_argument('--general-image-layouts-reference',dest='optimal_image_layouts',action='store_false',help='reference mode using GENERAL layouts for comparison')
descriptor_mode=parser.add_mutually_exclusive_group()
descriptor_mode.add_argument('--cache-store-descriptors',action='store_true',default=True,help='per-submission texture descriptor snapshots and empty-stage scan elision (default)')
descriptor_mode.add_argument('--uncached-store-descriptors-reference',dest='cache_store_descriptors',action='store_false',help='previous attachment index descriptor lookups for comparison')
copy_mode=parser.add_mutually_exclusive_group()
copy_mode.add_argument('--cpu-known-copies',action='store_true',default=True,help='immutable CPU producer snapshots; retains GPU copies and fence publication (default)')
copy_mode.add_argument('--gpu-readback-copies-reference',dest='cpu_known_copies',action='store_false',help='read GPU copy results back even when immutable CPU source bytes are known')
constant_mode=parser.add_mutually_exclusive_group()
constant_mode.add_argument('--cache-driver-constants',action='store_true',default=True,help='CPU-owned constant keys and last-content reuse (default)')
constant_mode.add_argument('--mapped-driver-constants-reference',dest='cache_driver_constants',action='store_false',help='compare mapped GPU constant bytes without last-content reuse')
parser.add_argument('--stream-submits',action=argparse.BooleanOptionalAction,default=True,help='submit bounded Vulkan batches at NVN target boundaries (default on; disable with --no-stream-submits)')
storage_mode=parser.add_mutually_exclusive_group()
storage_mode.add_argument('--storage-offset-bias',action='store_true',default=True,help='direct storage bindings where offline V3 metadata proves address compensation (default; otherwise original copy fallback)')
storage_mode.add_argument('--aligned-storage-copies-reference',dest='storage_offset_bias',action='store_false',help='retain aligned GPU copies for storage-buffer bindings')
pool_mode=parser.add_mutually_exclusive_group()
pool_mode.add_argument('--lazy-pool-sync',action='store_true',default=True,help='dependency-driven buffer uploads; skips unused pool pages (default)')
pool_mode.add_argument('--full-pool-sync-reference',dest='lazy_pool_sync',action='store_false',help='previous whole-pool upload scans for comparison')
readback_mode=parser.add_mutually_exclusive_group()
readback_mode.add_argument('--cached-readback', action='store_true', default=True, help='CPU-cached GPU readbacks with post-fence invalidation (default)')
readback_mode.add_argument('--coherent-readback-reference', dest='cached_readback', action='store_false', help='previous coherent uncached readback allocation for comparison')
cover_mode=parser.add_mutually_exclusive_group()
cover_mode.add_argument('--nonblocking-cover', action='store_true', default=True, help='optional coverage proofs never wait (default)')
cover_mode.add_argument('--cover-wait-reference', dest='nonblocking_cover', action='store_false', help='previous waiting coverage proof for diagnostic comparison')
stage_mode=parser.add_mutually_exclusive_group()
stage_mode.add_argument('--relax-graphics-stages', action='store_true', default=True, help='fragment-stage dependency with conservative fallback (default)')
stage_mode.add_argument('--graphics-stage-reference', dest='relax_graphics_stages', action='store_false', help='previous full-stage pass-start barrier for comparison')
parser.add_argument('--external-profile', action=argparse.BooleanOptionalAction, default=None, help='disable custom CPU/GPU timers (default except explicit validation/internal diagnostics)')
parser.add_argument('--submission-barrier-reference',action='store_true',help='previous full begin/end and pool-readback barriers; safe runtime comparison')
parser.add_argument('--dead-sample-queries-reference',action='store_true',help='retain queries whose counts are provably reset before any report')
parser.add_argument('--async-submit', action=argparse.BooleanOptionalAction, default=True, help='bounded asynchronous submission/completion (default on; serial reference uses --no-async-submit --no-stream-submits)')
parser.add_argument('--validate', action='store_true')
parser.add_argument('--bright-k', type=float, help='debug override for original DM_BRIGHT_K, from 1 to 4')
parser.add_argument('--game-res', help='original output resolution override WIDTHxHEIGHT; default is native Surface pixels')
parser.add_argument('--scale-3d', type=float, help='original DM_3D_SCALE override, from 0.5 to 1.0; default follows iOS 0.75 with its dynamic controller')
parser.add_argument('--fixed-3d', action='store_true', help='disable original dynamic 3D scale controller for comparisons')
query_mode=parser.add_mutually_exclusive_group()
query_mode.add_argument('--query-span', dest='query_span', action='store_true', default=True, help='Metal-style occlusion spans (default)')
query_mode.add_argument('--query-per-draw-reference', dest='query_span', action='store_false', help='retain the previous per-draw query implementation for comparisons')
parser.add_argument('--panel-freeze', action='store_true', help='debug opt-in to upstream DM_PANEL_FREEZE')
parser.add_argument('--runtime-extrap', action='store_true', help='debug opt-in to upstream DM_RT_AB=1')
parser.add_argument('--timeout', type=float, default=120)
parser.add_argument('--report', type=Path)
args = parser.parse_args()
if args.native_opus is None:
    args.native_opus = not args.opus_shadow
if args.external_profile is None:
    args.external_profile = not (args.gpu_profile or args.validate)
if args.attach and args.operation_mode is not None:
    parser.error('--operation-mode is startup-only and cannot be used with --attach')
if args.external_profile and (args.gpu_profile or args.validate):
    parser.error('--external-profile conflicts with --gpu-profile/--validate')
if args.stream_submits and not args.async_submit:
    parser.error('--stream-submits requires --async-submit')
if args.present_worker_shared_queue and not args.dedicated_present_worker:
    parser.error('--present-worker-shared-queue requires --dedicated-present-worker')
if args.timeout <= 0:
    parser.error('--timeout must be positive')
if args.bright_k is not None and not 1 <= args.bright_k <= 4:
    parser.error('--bright-k must be from 1 to 4')
if args.scale_3d is not None and not .5 <= args.scale_3d <= 1:
    parser.error('--scale-3d must be from 0.5 to 1.0')
if args.game_res is not None and (not re.fullmatch(r'[0-9]{1,4}x[0-9]{1,4}', args.game_res) or
        not all(16 <= int(n) <= 4096 for n in args.game_res.split('x'))):
    parser.error('--game-res must be WIDTHxHEIGHT with dimensions from 16 to 4096')
package = 'com.d2rmobile.nativeport'
base = ['adb', '-s', args.serial]


def adb(*parts, binary=False):
    return subprocess.check_output([*base, *parts], text=not binary,
                                   stderr=subprocess.DEVNULL, timeout=15)


def read(leaf):
    return adb('exec-out', 'run-as', package, 'cat', 'files/runtime/' + leaf)


# 抓帧工具负责启动时，只等待原游戏观测，不用重启覆盖工具的注入环境。
# Capture tools own launch/injection; attach only observes the original game.
if not args.attach:
    adb('shell', 'input', 'keyevent', 'WAKEUP')
    adb('shell', 'wm', 'dismiss-keyguard')
    time.sleep(.75)
    adb('shell', 'am', 'force-stop', package)
    launch = ['shell', 'am', 'start', '-W', '-n', package + '/.MainActivity',
              '--ez', 'run_game', 'true', '--ez', 'run_vulkan_tests', 'false',
              '--ez', 'gpu_profile', str(args.gpu_profile).lower(),
              '--ez', 'opus_shadow', str(args.opus_shadow).lower(),
              '--ez', 'native_opus', str(args.native_opus).lower(),
              '--ez', 'external_profile', str(args.external_profile).lower(),
              '--ez', 'async_submit', str(args.async_submit).lower(),
              '--ez', 'cpu_copy_reference', str(not args.cpu_known_copies).lower(),
              '--ez', 'driver_constant_reference', str(not args.cache_driver_constants).lower(),
              '--ez', 'sample_liveness_reference', str(args.dead_sample_queries_reference).lower(),
              '--ez', 'validate', str(args.validate).lower()]
    if args.operation_mode is not None:
        launch += ['--es', 'operation_mode', args.operation_mode]
    if args.bright_k is not None:
        launch += ['--ef', 'bright_k', str(args.bright_k)]
    if args.game_res is not None:
        launch += ['--es', 'game_res', args.game_res]
    if args.display_refresh_hz is not None:
        launch += ['--ef', 'display_refresh_hz', str(args.display_refresh_hz)]
    if args.scale_3d is not None:
        launch += ['--ef', 'scale_3d', str(args.scale_3d)]
    if args.fixed_3d:
        launch += ['--ez', 'fixed_3d', 'true']
    launch += ['--ez', 'query_per_draw_reference', str(not args.query_span).lower()]
    launch += ['--ez', 'storage_bias_reference', str(not args.storage_offset_bias).lower()]
    launch += ['--ez', 'pool_sync_reference', str(not args.lazy_pool_sync).lower()]
    launch += ['--ez', 'performance_hint_reference', str(not args.performance_hints).lower()]
    launch += ['--ez', 'image_layout_reference', str(not args.optimal_image_layouts).lower()]
    launch += ['--ez', 'sampled_array_layout_reference', str(args.general_sampled_array_layouts_reference).lower()]
    launch += ['--ez', 'buffer_memory_reference', str(args.global_buffer_memory_reference).lower()]
    launch += ['--ez', 'storage_usage_reference', str(args.storage_usage_reference).lower()]
    launch += ['--ez', 'attachment_usage_reference', str(args.attachment_usage_reference).lower()]
    launch += ['--ez', 'depth_layout_reference', str(args.general_depth_layouts_reference).lower()]
    launch += ['--ez', 'compute_layout_reference', str(args.general_compute_layouts_reference).lower()]
    launch += ['--ez', 'readonly_depth_reference', str(args.writable_depth_attachments_reference).lower()]
    launch += ['--ez', 'clear_attachment_reference', str(args.clear_attachments_reference).lower()]
    launch += ['--ez', 'texture_dirty_reference', str(args.texture_dirty_reference).lower()]
    launch += ['--ez', 'zero_draw_reference', str(args.zero_draw_reference).lower()]
    launch += ['--ez', 'no_fxaa', str(args.no_fxaa).lower()]
    launch += ['--ez', 'bloom_quarter', str(args.bloom_quarter).lower()]
    launch += ['--ez','mailbox_present',str(args.mailbox_present).lower()]
    launch += ['--ez','present_worker_reference',str(not args.dedicated_present_worker).lower()]
    launch += ['--ez','present_worker_shared_queue',str(args.present_worker_shared_queue).lower()]
    launch += ['--ez','descriptor_template_reference',str(args.descriptor_template_reference).lower()]
    launch += ['--ez','descriptor_history_reference',str(args.descriptor_history_reference).lower()]
    launch += ['--ez','descriptor_reuse_reference',str(args.descriptor_reuse_reference).lower()]
    launch += ['--ez','descriptor_batch_reference',str(args.descriptor_batch_reference).lower()]
    launch += ['--ez','dynamic_uniform_reference',str(args.dynamic_uniform_reference).lower()]
    launch += ['--ez','dynamic_uniform_narrow_reference',str(args.dynamic_uniform_narrow_reference).lower()]
    launch += ['--ez','pool_lookup_reference',str(args.pool_lookup_reference).lower()]
    launch += ['--ez','upload_scan_reference',str(args.upload_scan_reference).lower()]
    launch += ['--ez','sampler_lookup_reference',str(args.sampler_lookup_reference).lower()]
    launch += ['--ez','texture_binding_reference',str(args.texture_binding_reference).lower()]
    launch += ['--ez','texel_storage',str(args.texel_storage).lower()]
    if args.shader_present is not None:
        launch += ['--ez','shader_present',str(args.shader_present).lower()]
    launch += ['--ez','uniform_specialization',str(args.uniform_specialization).lower()]
    launch += ['--ez','uniform_predicate',str(args.uniform_predicate).lower()]
    launch += ['--ez','deferred_mip_clears',str(args.deferred_mip_clears).lower()]
    launch += ['--ez', 'custom_vulkan_driver', str(args.custom_vulkan_driver).lower()]
    launch += ['--ez', 'pool_mirror_reference', str(args.eager_pool_mirrors_reference).lower()]
    launch += ['--ez', 'stream_submit_reference', str(not args.stream_submits).lower()]
    launch += ['--ez', 'store_descriptor_reference', str(not args.cache_store_descriptors).lower()]
    launch += ['--ez', 'graphics_stage_reference', str(not args.relax_graphics_stages).lower()]
    launch += ['--ez', 'submission_barrier_reference', str(args.submission_barrier_reference).lower()]
    launch += ['--ez', 'cover_wait_reference', str(not args.nonblocking_cover).lower()]
    launch += ['--ez', 'readback_coherent_reference', str(not args.cached_readback).lower()]
    if args.panel_freeze:
        launch += ['--ez', 'panel_freeze', 'true']
    if args.runtime_extrap:
        launch += ['--ez', 'runtime_extrap', 'true']
    adb(*launch)
started = time.monotonic()
deadline = started + args.timeout
pid = None
selection_seen = None
entered = False
initial_observed = None
while time.monotonic() < deadline:
    try:
        current = adb('shell', 'pidof', package).strip()
        if pid and current != pid:
            raise SystemExit('native process exited or changed during launch')
        pid = current
        log = read('native.log')
        if not pid or f'Android native start pid={pid}\n' not in log:
            time.sleep(.25)
            continue
        if 'PanelManager OpenPanel HUDPanel' in log:
            lines = read('game-state.jsonl').splitlines()
            if lines:
                state = json.loads(lines[-1])
                observed = state.get('time_ns', 0)
                if initial_observed is None:
                    initial_observed = observed
                binding = state.get('native_bindings', {})
                output = state.get('game_output', {})
                surface = state.get('surface_output', {})
                if (state.get('valid') and state.get('world_input_ready') and
                        binding.get('ready') and state.get('present_interval_ms', 0) > 0 and
                        min(output.get('width', 0), output.get('height', 0),
                            surface.get('width', 0), surface.get('height', 0)) > 0 and
                        0 <= observed - binding.get('captured_ns', 0) < 1_000_000_000 and
                        0 <= observed - state.get('present_sample_ns', 0) < 1_000_000_000 and
                        (not args.attach or observed > initial_observed)):
                    external = 'external profiling: custom CPU/GPU timers disabled' in log
                    if args.external_profile and not external:
                        raise SystemExit('current process is not in the expected external-profile mode')
                    async_submit = '[vulkan] submission overlap: enabled' in log
                    if args.async_submit and not async_submit:
                        raise SystemExit('current process is not in the expected async submission mode')
                    hint=state.get('performance_hint',{})
                    if args.performance_hints:
                        if hint.get('available')==0 or hint.get('failures',0):
                            raise SystemExit('Android performance hint API unavailable or failed: '+json.dumps(hint))
                        if hint.get('reference',True) or hint.get('active',0)<1 or hint.get('reports',0)<1:
                            time.sleep(.25)
                            continue
                    if args.optimal_image_layouts and state.get('image_layouts',{}).get('reference',True):
                        raise SystemExit('optimal image layout policy is not active')
                    if args.stream_submits and state.get('stream_submits',{}).get('reference',True):
                        raise SystemExit('stream submission policy is not active')
                    if args.cpu_known_copies and state.get('cpu_known_copies',{}).get('reference',True):
                        raise SystemExit('CPU producer copy mode did not apply')
                    if args.cache_driver_constants and state.get('driver_constants',{}).get('reference',True):
                        raise SystemExit('driver constant cache policy is not active')
                    if not args.attach and state.get('sample_liveness',{}).get('reference') != args.dead_sample_queries_reference:
                        raise SystemExit('sample query liveness policy did not apply')
                    if not args.attach and state.get('submission_barriers',{}).get('reference') != args.submission_barrier_reference:
                        raise SystemExit('submission barrier policy did not apply')
                    if args.cache_store_descriptors and state.get('store_descriptors',{}).get('reference',True):
                        raise SystemExit('store descriptor cache policy is not active')
                    if args.game_res and [output['width'], output['height']] != [int(n) for n in args.game_res.split('x')]:
                        raise SystemExit('actual presented game output does not match --game-res')
                    if args.scale_3d is not None and abs(state['scale_3d'] - args.scale_3d) >= .001:
                        time.sleep(.25)
                        continue
                    report = {'pid': int(pid), 'launch_seconds': time.monotonic() - started,
                              'requested_opus_shadow':args.opus_shadow,
                              'requested_native_opus':args.native_opus,
                              'external_profile': external, 'attached': args.attach,
                              'async_submit': async_submit,
                              'performance_hints':not hint.get('reference',True) and hint.get('active',0)>0,
                              'optimal_image_layouts':not state.get('image_layouts',{}).get('reference',True),
                              'storage_usage_reference': (True if 'image storage usage: NVN IMAGE hint reference' in log else
                                                          False if 'image storage usage: deferred until actual binding' in log else None),
                              'attachment_usage_reference': (True if 'color attachment usage: format capability reference' in log else
                                                             False if 'color attachment usage: actual attachment only' in log else None),
                              'eager_pool_mirrors_reference':state.get('pool_mirrors',{}).get('reference'),
                              'depth_layout_reference': (True if 'depth/stencil layouts: GENERAL reference' in log else
                                                         False if 'depth/stencil layouts: attachment/read-only optimal' in log else None),
                              'compute_layout_reference': (True if 'compute sampled layouts: GENERAL reference' in log else
                                                           False if 'compute sampled layouts: read-only optimal' in log else None),
                              'stream_submits':not state.get('stream_submits',{}).get('reference',True),
                              'cache_store_descriptors':not state.get('store_descriptors',{}).get('reference',True),
                              'cpu_known_copies':not state.get('cpu_known_copies',{}).get('reference',True),
                              'cache_driver_constants':not state.get('driver_constants',{}).get('reference',True),
                              'elide_dead_sample_queries':not state.get('sample_liveness',{}).get('reference',True),
                              'async_present': '[vulkan] presentation overlap: enabled' in log,
                              'shader_chain': read('shaders/.chain').strip(),
                              'native_host_opt_level': (int(re.search(r'Android native host optimization=-O([123])\b', log).group(1))
                                                        if re.search(r'Android native host optimization=-O([123])\b', log) else None),
                              'requested_game_res': args.game_res, 'requested_scale_3d': args.scale_3d,
                              'requested_fixed_3d': args.fixed_3d,
                              'original_character_selection': 'PanelManager OpenPanel CharacterSelectPanel' in log, 'original_hud': True,
                              'actual_game_state': state}
                    if args.report:
                        args.report.parent.mkdir(parents=True, exist_ok=True)
                        args.report.write_text(json.dumps(report, indent=2) + '\n')
                    print(json.dumps(report))
                    break
        if not entered and 'PanelManager OpenPanel HUDPanel' not in log:
            if 'PanelManager OpenPanel CharacterSelectPanel' in log:
                selection_seen = selection_seen or time.monotonic()
            if selection_seen and time.monotonic() - selection_seen >= 3:
                png = adb('exec-out', 'screencap', '-p', binary=True)
                if png[:8] != b'\x89PNG\r\n\x1a\n':
                    raise SystemExit('invalid Android screenshot dimensions')
                width, height = struct.unpack('>II', png[16:24])
                if width <= height:
                    raise SystemExit('game Surface is not landscape')
                adb('shell', 'input', 'tap', str(round(width / 2)), str(round(height * .9)))
                entered = True
                print(f'pid={pid}: original character selection ready, tapped Enter Game', flush=True)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, json.JSONDecodeError):
        pass
    time.sleep(.4)
else:
    raise SystemExit('timed out waiting for fresh original world/binding/presentation observations')
