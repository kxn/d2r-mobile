This debug-only app runs the unchanged official Arm Performance Studio 2026.5
Android GFXReconstruct replay library against the previously captured original
ARM64 game's Vulkan calls. It does not execute guest modules, emulate a CPU,
replace a renderer, or measure FPS. There is no Present in the trace: the 25
submit capture contains 27 submissions including reconstruction and zero frames.

The existing verified adrenotools DRIVER_CUSTOM -> Android loader route is
selected through the official GFXRECON_VULKAN_LIBRARY_PATH tool option. Exact
private ICD executable inode and driverID20 are required. No GPU_MAPPING_IMPORT,
host-import extension or feature filtering is enabled. Original fork c5ec is
unchanged. Only a missing depth-resolve -> create_renderpass2 dependency in the
existing official replay utility is supplied at its vkCreateDevice boundary;
features and pNext are unchanged. This is tool glue, not a driver fix.

The official Arm replay .so and six frozen hook/VVL libraries are copied without
modification. New code is limited to an Android NativeActivity entry, cold
loader/environment/log setup, exact identity checking, and that dependency.
The default is an offline plan. The independent ordinary UID package is required
to be absent; production package/files are read only. One launch, original
failure preservation, strict first PID/UID ownership, finite timeout, exact
owned cleanup, and full production35/driver+shader inventories before/after are
mandatory. No global settings or original game files are written.

--validate requests official VVL; VK_LAYER_ENABLES requests official sync
validation. Actual layer banner/errors are evidence; the request alone is not
proof. VUIDs remain failures/diagnostics. Official SDK normal exit uses SIGTERM;
EOF/zero frames are not automatically proof of correct pixels or original-game
acceptance. Replay may fail to reproduce original concurrency, and captured
state reconstruction can itself expose tool/driver errors. Do not label replay
success a game success or absence of a hang proof that no driver bug exists.

The Arm SDK reports GFXReconstruct 1.0.5 (2bb4438); the local official tool source
280a0ac is correlated for entry semantics, not claimed byte-identical SDK source.
Previous LVP cross-GPU attempts have known memory-translation/replay utility
limitations. This same-device run uses --memory-translation none and does not
remove unsupported features or change driver capabilities.
