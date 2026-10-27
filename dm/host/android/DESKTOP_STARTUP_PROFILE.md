# Verified desktop startup, schemas 1, 2, 3, 4 and 5

This corrects a delivery gap: transaction tests restore their original APK and
assets; that original APK predates shared/native resources. Successful test
extras are not a persistent installation. Keep transaction restoration intact.

Only a normal MAIN/LAUNCHER Intent with no explicit extras may consume the private
`files/runtime/launch-profile.properties`. Explicit tests and launch commands
remain independent. Validate once before native startup; reuse the immutable
selection on Activity recreation. No file/hash work occurs in the render loop.
An absent file retains the original platform defaults. An invalid installed
profile must produce a short launch error, not silently run a different backend.

Use Java's standard UTF-8 Properties reader, bounded to 16 KiB. All keys below
are required, unknown keys are rejected. SHA values are lowercase 64 hex digits.
Paths are relative to the app-private runtime directory and cannot escape it,
including through symlinks. Device fingerprint is the exact Android build
fingerprint qualified by the publisher, not a GPU/model guess.

```
schema=1
name=Turnip shared/native
apk_sha256=<actual installed APK SHA256>
device_fingerprint=<actual ro.build.fingerprint>
driver_sha256=<runtime/vulkan-driver/libvulkan_freedreno.so SHA256>
modules_manifest_sha256=<runtime/modules/rewrite-manifest.json SHA256>
shader_chain_sha256=<runtime/shaders/.chain SHA256>
native_table=native-one-resources/native-layouts.bin
native_table_sha256=<actual table SHA256>
native_pack_sha256=<existing authenticated native pack content SHA256>
native_producer_sha256=<existing producer source SHA256>
receipt_sha256=<runtime/launch-profile-receipt.json SHA256>
vulkan_provider=adreno
shared_guest_heap=true
pool_window_bytes=1048576
shared_lazy_windows=true
shared_image_raw_observe=false
driver_span_reference=true
pixel_local=true
pixel_local_shared_ds=false
```

Schema 1 represents the already qualified shared/native Turnip combination;
it does not enable unverified providers or scan directories for usable drivers.
The relative native table location is selected by the publisher from its actual
installed inventory, not assumed from the example above. Existing native table,
pack, provider UUID and capability checks continue unchanged. Java checks the
APK, driver, module manifest, shader-chain marker, table and publication receipt;
it does not replace full publication inventory verification or native payload
authentication. SHA reads use a bounded streaming buffer and retain no snapshots.

Schema 2 retains every schema 1 field and requires exactly four additional
typed fields (set `schema=2` in the same properties file):

```
inline_translation=true
translation_cpu_mask=128
stream_submits=false
turnip_autotune=prefer_sysmem
```

Schema 3 retains every schema 2 field and additionally requires the strict
boolean `turnip_swapchain_ubwc`. A qualified publication may set it to `true`
for its exact driver/device tuple. It sets `TU_UBWC_SWAPCHAIN` only inside the
app process, before driver initialization, without changing Android properties.
The default driver policy remains disabled; absent explicit selection preserves
an externally configured experiment. The allocation request is not proof of
actual compression: retain the allocator/mapper layout and real-game checks in
the publication evidence. Old schemas continue rejecting this extra key.

Schema 4 retains exactly every schema 3 field and requires one additional field
(set `schema=4` in the same properties file):

```
guest_wait_budget_us=30
```

The value is a canonical decimal integer from 0 through 100, in microseconds.
Signs, leading zeroes (except `0`), whitespace within the parsed value,
nondecimal text and overflow are rejected. The loader emits an `Integer` cold
extra for the existing native startup setter. Zero retains direct condition
waiting; a positive budget selects the existing bounded primary-thread polling
phase before falling back to that wait. This does not change guest lock
ordering, timeout/cancellation semantics, or user display/thermal settings.
Polling has a CPU cost, so the publisher must qualify the selected budget for
the exact APK/device tuple rather than infer a general platform default.

Schemas 1, 2 and 3 still accept only their original exact field sets. They do not
emit this extra and retain the runtime's zero-budget default; they reject
`guest_wait_budget_us` even when its value is `0`. Schema 4 cannot omit it or any
inherited field. All existing identity, SHA, path and receipt checks apply.

Schema 5 retains every schema 4 field and additionally requires the strict
boolean `record_state_cache` (set `schema=5` in the same properties file):

```
record_state_cache=true
```

Only lowercase `true` or `false` is accepted. The loader emits a `Boolean` cold
extra for the existing NVN recording-state cache setter. This cache removes
identical fixed-state recordings on each command buffer; it does not cache game
tasks or bypass resource synchronization. The publisher must explicitly select
and qualify the value for its APK/device tuple. Missing or mistyped values are
rejected, as are missing inherited fields; all APK, payload, device, path and
receipt verification remains required.

Without an installed profile the cache retains its runtime default of off.
Schemas 1–4 likewise emit no `record_state_cache` extra and keep that default;
they reject this additional field even when it is `false`. Explicit CLI or test
Intents still bypass the desktop profile, including an explicit
`--no-record-state-cache` launch. A schema 5 publication does not turn on the
cache globally or alter the existing cold-start-only rule.

These values describe the locally qualified Q735 combination, not universal
device defaults. The CPU mask is a canonical nonnegative decimal Java integer;
128 selects CPU 7 on that device. `turnip_autotune` accepts only `default`,
`bandwidth`, `profiled`, or `prefer_sysmem`. The selected value is cached before
Vulkan initialization as an app-private driver environment setting; it does not
write global Android/Mesa properties. `default` preserves external driver policy.
`prefer_sysmem` is Turnip's existing adaptive render-pass policy, not a force-all
SYSMEM mode. The stream field selects the existing heuristic submission policy;
false retains guest-required submissions and asynchronous GPU completion.

Schema 1 remains backward compatible and does not imply these four selections.
No schema overrides user resolution, 3D scale, FPS, or thermal controls.
Any change to the APK or payload tuple requires verified matching hashes and a
new receipt before ordinary desktop qualification.

Apply existing cold extras for provider, shared heap, native table/pack/producer,
window, lazy windows, raw observation, driver span and pixel-local selection.
Use async game startup without validation/test modes. Dynamic user settings,
including resolution, scale, FPS and thermal policy, stay under the existing HUD.

The publisher is a separate, Q-local transaction based on a new root-reviewed
delivery receipt. Reuse the accepted installer, inventories, directory CAS,
APK signer, original NSO provenance and save protocol. The original schema 1
delivery used freshly rebuilt modules, the f830 driver, full2838 native
overlay/table and 950 shader bytes. A subsequent qualified tuple must name and
verify its actual inventory rather than silently inherit these historical
versions. Publish the matching APK/payload set and receipt before the profile.
Do not change old `production_default_enable=false`/`deploy_allowed=false`
records. No global device/default approval follows from this local delivery.
Success requires two actual ordinary desktop cold starts with the selected
backend/shared/native policy observed, then original lifecycle saves and stops.
