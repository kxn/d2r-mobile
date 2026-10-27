#!/system/bin/sh
# Existing Mesa/IR3 compiler diagnostics only. Keep normal caches intact and
# force compilation into a private diagnostic directory, without changing
# shaders, driver code, guest modules or any rendering algorithm.
mesa_diagnostic_cache=/data/user/0/com.d2rmobile.nativeport/files/runtime/temp/mesa-shader-disasm-cache-$$
mkdir -p "$mesa_diagnostic_cache" || exit 1
export DM_VULKAN_PIPELINE_CACHE_DIR="$mesa_diagnostic_cache"
export MESA_SHADER_CACHE_DISABLE=true
export IR3_SHADER_DEBUG=vs,fs
export MESA_LOG=file
export MESA_LOG_LEVEL=info
export MESA_LOG_FILE=/data/user/0/com.d2rmobile.nativeport/files/runtime/mesa-shader-disasm.log
exec "$@"
