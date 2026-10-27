#!/system/bin/sh
# Diagnostic packaging only. Android's debuggable-app wrapper sets Mesa's
# existing GPU tracing before the driver initializes. No guest or NVN changes.
# Use pass/command-buffer scopes: tiled GPU draw timestamps are not isolated
# shader times. Keep per-draw tracing off and never use capture FPS as baseline.
export MESA_GPU_TRACES=print_csv
export MESA_GPU_TRACEFILE=/data/user/0/com.d2rmobile.nativeport/files/runtime/mesa-gpu-trace.csv
export TU_GPU_TRACEPOINT=cmd_buffer,render_pass,binning_ib,draw_ib_sysmem,draw_ib_gmem,gmem_load,gmem_store,sysmem_resolve,blit_image,clear_blit,compute,compute_indirect
exec "$@"
