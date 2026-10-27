#!/system/bin/sh
# Standard Mesa/IR3 diagnostics only. Draw timestamps identify stage hashes;
# they must not be ranked as isolated shader costs on a tiled GPU.
# One owned directory keeps the normal pipeline cache and previous logs intact.
mesa_diagnostic_root=/data/user/0/com.d2rmobile.nativeport/files/runtime/temp/mesa-draw-identities-$$
mkdir -p "$mesa_diagnostic_root/pipeline-cache" || exit 1
export DM_VULKAN_PIPELINE_CACHE_DIR="$mesa_diagnostic_root/pipeline-cache"
export MESA_SHADER_CACHE_DISABLE=true
export IR3_SHADER_DEBUG=vs,fs
export MESA_LOG=file
export MESA_LOG_LEVEL=info
export MESA_LOG_FILE="$mesa_diagnostic_root/ir3.log"
export MESA_GPU_TRACES=print_csv
export MESA_GPU_TRACEFILE="$mesa_diagnostic_root/draw-identities.csv"
export TU_GPU_TRACEPOINT=cmd_buffer,render_pass,binning_ib,draw_ib_sysmem,draw_ib_gmem,gmem_load,gmem_store,sysmem_resolve,blit_image,clear_blit,compute,compute_indirect,draw
exec "$@"
