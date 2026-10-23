# Verified desktop startup, schema 1

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

Apply existing cold extras for provider, shared heap, native table/pack/producer,
window, lazy windows, raw observation, driver span and pixel-local selection.
Use async game startup without validation/test modes. Dynamic user settings,
including resolution, scale, FPS and thermal policy, stay under the existing HUD.

The publisher is a separate, Q-local transaction based on a new root-reviewed
delivery receipt. Reuse the accepted installer, inventories, directory CAS,
APK signer, original NSO provenance and save protocol. Publish the APK, freshly
rebuilt modules, f830 driver, full2838 native overlay/table and this file as one
matched set; publish the profile last. Preserve the existing 950 shader bytes.
Do not change old `production_default_enable=false`/`deploy_allowed=false`
records. No global device/default approval follows from this local delivery.
Success requires two actual ordinary desktop cold starts with the selected
backend/shared/native policy observed, then original lifecycle saves and stops.
