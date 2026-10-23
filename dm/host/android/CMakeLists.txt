cmake_minimum_required(VERSION 3.20)
project(dmhost_android C ASM CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../native_thin_lto.cmake")
set(CMAKE_CXX_STANDARD 17)
add_executable(native_display_test tests/native_display.c display.c)
target_compile_options(native_display_test PRIVATE -UNDEBUG)
target_link_options(native_display_test PRIVATE -Wl,-z,max-page-size=16384)
option(DM_ANDROID_TLS_DESC "Load the APK with native dynamic TLSDESC instead of preload TLS" OFF)
option(DM_ANDROID_RENDERDOC_API "Optional official RenderDoc capture integration" OFF)
set(DM_ANDROID_RUNTIME_OPT_LEVEL "3" CACHE STRING "Native runtime optimization level (1, 2, 3)")
if(NOT DM_ANDROID_RUNTIME_OPT_LEVEL MATCHES "^[123]$")
  message(FATAL_ERROR "DM_ANDROID_RUNTIME_OPT_LEVEL must be 1, 2 or 3")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  message(FATAL_ERROR "Native host requires ARM64 hardware; CPU emulation is forbidden (AGENTS.md)")
endif()
set(CMAKE_C_STANDARD 11)
set(HOST "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SHA "${HOST}/../third_party/runtime-codecs")
set(NATIVE_CORE
  platform_elf.c headless.c display.c
  "${HOST}/loader.c" "${HOST}/hle.c" "${HOST}/entry.S"
  "${HOST}/thread.c" "${HOST}/hle_os.c" "${HOST}/hle_svc.c"
  "${HOST}/hle_thread.c" "${HOST}/hle_sdkinit.c" "${HOST}/hle_table.c"
  "${HOST}/hle_count.c" "${SHA}/sha256.c" "${SHA}/sha1.c")
add_executable(dmhost_native main.c graphics_boundary.c ${NATIVE_CORE} "${HOST}/hle_oe.c" "${HOST}/hle_time.c" "${HOST}/hle_net.c" "${HOST}/hle_account.c" "${HOST}/hle_friends.c" "${HOST}/hle_misc.c" "${HOST}/../gfx/hle_vi.c" "${HOST}/../gfx/hle_nv.c" "${HOST}/hle_hid.c" "${HOST}/hle_touch.c" "${HOST}/hle_fs.c" "${HOST}/dm_i18n.c" platform_fs.c "${HOST}/hle_tact.c" "${SHA}/md5.c"
  "${HOST}/../third_party/libdeflate/lib/deflate_decompress.c"
  "${HOST}/../third_party/libdeflate/lib/zlib_decompress.c"
  "${HOST}/../third_party/libdeflate/lib/adler32.c"
  "${HOST}/../third_party/libdeflate/lib/utils.c"
  "${HOST}/../third_party/libdeflate/lib/arm/cpu_features.c")
add_executable(native_rule_test tests/native_tests.c tests/native_host_stack.S ${NATIVE_CORE})
add_library(native_tlsdesc_test SHARED tests/native_tests.c tests/native_host_stack.S ${NATIVE_CORE})
target_compile_definitions(native_tlsdesc_test PRIVATE DM_TLS_DESC=1 main=dm_native_fixture_main)
target_compile_options(native_tlsdesc_test PRIVATE -fno-emulated-tls)
add_executable(native_dlopen_test tests/native_dlopen.c)
target_link_libraries(native_dlopen_test PRIVATE dl)
target_link_options(native_dlopen_test PRIVATE -Wl,-z,max-page-size=16384)
foreach(target dmhost_native native_rule_test native_tlsdesc_test)
target_include_directories(${target} PRIVATE "${HOST}" "${SHA}")
target_compile_definitions(${target} PRIVATE _GNU_SOURCE DM_STRICT_HLE=1 DM_HEADLESS=1 MBEDTLS_CONFIG_FILE="dm_sha_config.h")
target_compile_options(${target} PRIVATE -O1 -g -fno-omit-frame-pointer -Wall -Wno-format)
target_link_options(${target} PRIVATE -Wl,-z,max-page-size=16384 -Wl,--no-undefined -Wl,--gc-sections)
target_link_libraries(${target} PRIVATE m dl)
endforeach()

add_executable(native_fs_test "${HOST}/tests/t_fs.c" "${HOST}/tests/t_fs_stub.c" "${HOST}/dm_i18n.c" platform_fs.c)
target_include_directories(native_fs_test PRIVATE "${HOST}")
target_compile_definitions(native_fs_test PRIVATE _GNU_SOURCE)
target_compile_options(native_fs_test PRIVATE -O1 -g -Wall -Wno-unused-function)
target_link_options(native_fs_test PRIVATE -Wl,-z,max-page-size=16384 -Wl,--gc-sections)
add_executable(native_touch_test "${HOST}/tests/t_touch.c" "${HOST}/hle_touch.c")
add_executable(native_hid_test tests/native_hid.c "${HOST}/hle_touch.c")
foreach(target native_touch_test native_hid_test)
  target_include_directories(${target} PRIVATE "${HOST}")
  target_compile_definitions(${target} PRIVATE _GNU_SOURCE DM_HEADLESS=1)
  target_compile_options(${target} PRIVATE -O1 -g -Wno-unused-function)
  target_link_options(${target} PRIVATE -Wl,-z,max-page-size=16384 -Wl,--gc-sections)
endforeach()

# 原 NVN 接口与 Vulkan 后端单独构建；保留边界目标供原生引导回归。
# Build the original NVN interface with Vulkan separately; keep the boundary target for native bring-up regression.
set(GFX "${HOST}/../gfx")
add_executable(native_pace_test "${GFX}/vulkan/tests/pace.c" "${GFX}/vulkan/nvn_vk_pace.c")
target_link_libraries(native_pace_test PRIVATE m)
target_link_options(native_pace_test PRIVATE -Wl,-z,max-page-size=16384)
include("${GFX}/vulkan/builtins.cmake")
set(VK_SOURCES "${GFX}/vulkan/nvn_vk_freeze.c" "${GFX}/vulkan/nvn_vk_bright.c" "${GFX}/vulkan/nvn_vk_ghost.c" "${GFX}/vulkan/nvn_vk_capture.c" "${GFX}/vulkan/nvn_vk_window.c" "${GFX}/vulkan/nvn_vk_draw.c" "${GFX}/vulkan/nvn_vk_state.c" "${GFX}/vulkan/nvn_vk_shader.c" "${GFX}/vulkan/nvn_vk_counter.c" "${GFX}/vulkan/nvn_vk_query.c" "${GFX}/vulkan/nvn_vk.c" "${GFX}/vulkan/nvn_vk_format.c" "${GFX}/vulkan/nvn_vk_replay.c" "${GFX}/vulkan/nvn_vk_texture.c" "${GFX}/nvn/nvn_platform.c" "${GFX}/nvn/nvn_tex.c" "${GFX}/nvn/nvn_native_layout.c" "${GFX}/tex/nvn_deswizzle.c" "${GFX}/tex/nvn_bcdec.c")
get_target_property(NATIVE_SOURCES dmhost_native SOURCES)
list(APPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_record_owner.c")
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_pace.c")
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_memory.cpp" "${GFX}/tex/nvn_astc_metadata.cpp")
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_loader.c" "${GFX}/../third_party/volk/volk.c")
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_plan.c")
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_event.c")
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_pipeline_cache.c")
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_renderdoc.c")
if(DM_ANDROID_RENDERDOC_API)
  if(NOT EXISTS "${DM_RENDERDOC_INCLUDE_DIR}/renderdoc_app.h")
    message(FATAL_ERROR "Set DM_RENDERDOC_INCLUDE_DIR to the installed official RenderDoc SDK include directory")
  endif()
  add_compile_definitions(DM_RENDERDOC_API=1)
  include_directories("${DM_RENDERDOC_INCLUDE_DIR}")
endif()
list(PREPEND VK_SOURCES "${GFX}/vulkan/nvn_vk_extrap.c" "${GFX}/vulkan/nvn_vk_runtime.c")
list(REMOVE_ITEM NATIVE_SOURCES graphics_boundary.c)
list(APPEND NATIVE_SOURCES "${HOST}/hle_private_aim.c")
add_executable(dmhost_vulkan uimsg.c game_state.c game_calls.c "${HOST}/dm_world.c" "${HOST}/dm_uiscreen.c" "${HOST}/dm_townwp.c" "${HOST}/hle_swkbd.c" "${HOST}/hle_audio.c" "${HOST}/hle_codec.c" "${HOST}/hle_granny.c" ${NATIVE_SOURCES} ${VK_SOURCES} "${GFX}/nvn/hle_nvn.c" "${GFX}/nvn/nvn_entry.S" "${GFX}/nvn/nvn_thunks.S" "${GFX}/nvn/nvn_dirty.c")
target_include_directories(dmhost_vulkan PRIVATE "${HOST}" "${SHA}" "${GFX}/nvn/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(dmhost_vulkan PRIVATE DM_NATIVE_WORLD_CORE=1 DM_NATIVE_GRAPHICS=1 DM_NATIVE_GRANNY=1 DM_NATIVE_GAME_CALLS=1 _GNU_SOURCE DM_STRICT_HLE=1 DM_HEADLESS=1 MBEDTLS_CONFIG_FILE="dm_sha_config.h")
target_compile_options(dmhost_vulkan PRIVATE -O${DM_ANDROID_RUNTIME_OPT_LEVEL} -g -fno-omit-frame-pointer -Wall -Wno-format)
target_compile_definitions(dmhost_vulkan PRIVATE DM_ANDROID_RUNTIME_OPT_LEVEL=${DM_ANDROID_RUNTIME_OPT_LEVEL} VK_USE_PLATFORM_ANDROID_KHR=1)
target_link_options(dmhost_vulkan PRIVATE -Wl,-z,max-page-size=16384 -Wl,--no-undefined -Wl,--gc-sections)
target_link_libraries(dmhost_vulkan PRIVATE m dl aaudio android)

# Optional, defaultSDK shadow/nativecore candidate, static and source-pinned.
# Nativecore is explicit; SDK parsing/capacity/errors/mutex remain original.
include("${HOST}/../third_party/opus_fixed.cmake")
dm_add_fixed_opus()
add_executable(native_opus_shadow_test "${HOST}/tests/t_opus_shadow.c")
target_compile_definitions(native_opus_shadow_test PRIVATE DM_OPUS_SHADOW_OWNED_HOST=1 _GNU_SOURCE)
target_compile_options(native_opus_shadow_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_include_directories(native_opus_shadow_test PRIVATE "${HOST}")
target_link_libraries(native_opus_shadow_test PRIVATE opus m)
target_link_options(native_opus_shadow_test PRIVATE -Wl,-z,max-page-size=16384)
add_executable(native_opus_tail_test tests/native_opus_tail.c tests/native_opus_tail.S "${HOST}/opus_core_entry.S")
target_compile_options(native_opus_tail_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_options(native_opus_tail_test PRIVATE -Wl,-z,max-page-size=16384)
target_sources(dmhost_vulkan PRIVATE "${HOST}/opus_shadow.c" "${HOST}/opus_core_entry.S")
target_link_libraries(dmhost_vulkan PRIVATE opus)
target_link_options(dmhost_vulkan PRIVATE -Wl,--exclude-libs,libopus.a)

target_sources(dmhost_vulkan PRIVATE performance_hint.c)
get_target_property(VULKAN_SOURCES dmhost_vulkan SOURCES)
list(REMOVE_ITEM VULKAN_SOURCES main.c)
add_executable(native_vulkan_test tests/native_vulkan_runner.c tests/native_vulkan.c ${VULKAN_SOURCES})
foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES)
  get_target_property(values dmhost_vulkan ${property})
  set_property(TARGET native_vulkan_test PROPERTY ${property} ${values})
endforeach()
target_compile_definitions(native_vulkan_test PRIVATE DM_VULKAN_TEST_EMBED=1)

# Actual original SDK-only codec oracle; never invokes nnMain or any NVN API.
add_executable(native_opus_sdk_test tests/native_opus_sdk.c ${VULKAN_SOURCES})
foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES)
  get_target_property(values dmhost_vulkan ${property})
  set_property(TARGET native_opus_sdk_test PROPERTY ${property} ${values})
endforeach()
target_compile_definitions(native_opus_sdk_test PRIVATE DM_TLS_DESC=1)
target_compile_options(native_opus_sdk_test PRIVATE -fno-emulated-tls -UNDEBUG)
add_dependencies(native_opus_sdk_test dm_vulkan_builtins)

# Match the APK's dynamic-TLSDESC linkage.  In a PIE executable LLD relaxes
# dm_tsd_descriptor's address expression to a local-exec offset (possibly zero),
# which is not a descriptor address.  Keep the old executable only as a recorded
# negative bootstrap case; the SDK oracle is run through this shared library.
add_library(native_opus_sdk_fixture SHARED tests/native_opus_sdk.c ${VULKAN_SOURCES})
foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES)
  get_target_property(values dmhost_vulkan ${property})
  set_property(TARGET native_opus_sdk_fixture PROPERTY ${property} ${values})
endforeach()
target_compile_definitions(native_opus_sdk_fixture PRIVATE DM_TLS_DESC=1 main=dm_opus_sdk_fixture_main)
target_compile_options(native_opus_sdk_fixture PRIVATE -fno-emulated-tls -UNDEBUG -fvisibility=hidden)
target_link_options(native_opus_sdk_fixture PRIVATE -Wl,-Bsymbolic)
add_dependencies(native_opus_sdk_fixture dm_vulkan_builtins)
add_executable(native_opus_sdk_runner tests/native_opus_sdk_runner.c)
target_compile_options(native_opus_sdk_runner PRIVATE -Wall -Wextra -Werror)
target_link_libraries(native_opus_sdk_runner PRIVATE dl)
target_link_options(native_opus_sdk_runner PRIVATE -Wl,-z,max-page-size=16384)

add_library(dmhost_android SHARED jni.c input.c touch.c touch_ui.c "${HOST}/../ios/Sources/dm_touch_controls.c" text_input.c tests/native_vulkan.c main.c ${VULKAN_SOURCES})
foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES)
  get_target_property(values dmhost_vulkan ${property})
  set_property(TARGET dmhost_android PROPERTY ${property} ${values})
endforeach()
target_compile_definitions(dmhost_android PRIVATE main=dm_android_main DM_VULKAN_TEST_EMBED=1 DM_ANDROID_APP=1)
target_compile_options(dmhost_android PRIVATE -fvisibility=hidden)
target_link_options(dmhost_android PRIVATE -Wl,-Bsymbolic)
target_link_libraries(dmhost_android PRIVATE android)
function(dm_add_driver_tools)
  set(BUILD_SHARED_LIBS ON)
  add_subdirectory("${GFX}/../third_party/libadrenotools" adrenotools)
endfunction()
dm_add_driver_tools()
set(DRIVER_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/vulkan-loader-libs")
add_custom_target(dm_driver_libraries
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DRIVER_LIBRARY_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:adrenotools>" "${DRIVER_LIBRARY_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:main_hook>" "${DRIVER_LIBRARY_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:hook_impl>" "${DRIVER_LIBRARY_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:file_redirect_hook>" "${DRIVER_LIBRARY_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gsl_alloc_hook>" "${DRIVER_LIBRARY_DIR}/"
  DEPENDS adrenotools main_hook hook_impl file_redirect_hook gsl_alloc_hook VERBATIM)
add_dependencies(dmhost_android dm_driver_libraries)
foreach(target dmhost_vulkan native_vulkan_test dmhost_android)
  add_dependencies(${target} dm_vulkan_builtins)
endforeach()

# 原宿主模块直接链接 APK；单独编译此目标不算真机功能验收。
# Link original feature modules into the APK. Compiling this target alone
# does not constitute physical-device feature acceptance.
set(ORIGINAL_FEATURES
  actions autobuff beltauto bossrush bright cloudsync cloudmerge cursor
  cursor_item custom dropfilter drophaptic elysium freetp hud kills lootfilter
  namegen padmode pages potpick pouch rift rift_ui runeword rwlv_core shoprefresh
  textbox textbox_core textinput turnsmooth update vault vault_ui prof)
set(ORIGINAL_FEATURE_SOURCES)
foreach(feature IN LISTS ORIGINAL_FEATURES)
  list(APPEND ORIGINAL_FEATURE_SOURCES "${HOST}/dm_${feature}.c")
endforeach()
# 原 Makefile 从用户自己的 RomFS 生成这三个表；不提交游戏派生数据。
# Generate original tables from the user's RomFS through the upstream Makefile;
# no game-derived table is checked in or replaced with an empty placeholder.
find_program(DM_MAKE_EXECUTABLE make REQUIRED)
if(DEFINED ENV{DM_ROMFS_SRC})
  set(DM_TABLE_ROMFS "$ENV{DM_ROMFS_SRC}")
elseif(DEFINED ENV{DM_ROOT})
  set(DM_TABLE_ROMFS "$ENV{DM_ROOT}/data/switch/3.3.94012/Update/RomFS")
else()
  get_filename_component(DM_TABLE_ROMFS "${HOST}/../../work/data/switch/3.3.94012/Update/RomFS" ABSOLUTE)
endif()
set(DM_ORIGINAL_ROMFS "${DM_TABLE_ROMFS}" CACHE PATH "Original RomFS for private generated host tables")
add_custom_target(dm_original_tables
  COMMAND "${DM_MAKE_EXECUTABLE}" -C "${HOST}" "ROMFS=${DM_ORIGINAL_ROMFS}"
          dm_vault_tab.h dm_hud_exp.h dm_bright_levels.inc
  VERBATIM)
add_library(dm_original_features OBJECT EXCLUDE_FROM_ALL ${ORIGINAL_FEATURE_SOURCES} "${HOST}/diag.c" "${HOST}/crash.c" "${HOST}/hle_target.c" "${HOST}/dm_boot.c" "${HOST}/hle_guestthread.c"
  "${HOST}/../ios/Sources/dm_touch_game_bridge.c"
  "${HOST}/../ios/Sources/dm_touch_game_identity.c"
  "${HOST}/../ios/Sources/dm_touch_game_state.c"
  "${HOST}/../ios/Sources/dm_touch_native_art.c")
target_include_directories(dm_original_features PRIVATE "${HOST}" "${SHA}")
target_compile_definitions(dm_original_features PRIVATE _GNU_SOURCE DM_HEADLESS=1 DM_NATIVE_FEATURES=1 DM_NATIVE_GRAPHICS=1 DM_NATIVE_WORLD_CORE=1 MBEDTLS_CONFIG_FILE="dm_sha_config.h")
target_compile_options(dm_original_features PRIVATE -O${DM_ANDROID_RUNTIME_OPT_LEVEL} -g -Wall -Wno-format -ffunction-sections -fdata-sections -fvisibility=hidden)

target_compile_definitions(dmhost_android PRIVATE DM_NATIVE_FEATURES=1)
target_sources(dmhost_android PRIVATE $<TARGET_OBJECTS:dm_original_features>)
if(DM_ANDROID_TLS_DESC)
  foreach(target dmhost_android dm_original_features)
    target_compile_definitions(${target} PRIVATE DM_TLS_DESC=1)
    target_compile_options(${target} PRIVATE -fno-emulated-tls)
  endforeach()
endif()

# Keep all C/C++ participating in the application host visible to ThinLTO.
# Prebuilt driver/helper libraries and original game modules are not rewritten.
dm_native_thinlto(dmhost_android)
dm_native_thinlto(dm_original_features)

add_dependencies(dm_original_features dm_original_tables)

set_source_files_properties("${HOST}/../ios/Sources/dm_touch_native_art.c" PROPERTIES GENERATED TRUE)
add_custom_target(dm_original_assets
  COMMAND bash "${HOST}/../../scripts/dev/build-original-assets.sh"
  VERBATIM)
add_dependencies(dm_original_features dm_original_assets)

option(DM_VULKAN_SHARED_POOLS "Build fixed lazy whole shared-pool experiment" OFF)
if(DM_VULKAN_SHARED_POOLS)
  foreach(shared_target dmhost_android native_vulkan_test)
    target_compile_definitions(${shared_target} PRIVATE DM_VULKAN_SHARED_POOLS=1)
  endforeach()
endif()

option(DM_VULKAN_SHARED_IMPORT_WINDOWS "Build experimental canonical shared imports (cold nonzero window bytes)" OFF)
if(DM_VULKAN_SHARED_IMPORT_WINDOWS)
  if(NOT DM_VULKAN_SHARED_POOLS)
    message(FATAL_ERROR "DM_VULKAN_SHARED_IMPORT_WINDOWS requires DM_VULKAN_SHARED_POOLS")
  endif()
  target_compile_definitions(dmhost_android PRIVATE DM_VULKAN_SHARED_IMPORT_WINDOWS=1)
  target_compile_definitions(native_vulkan_test PRIVATE DM_VULKAN_SHARED_IMPORT_WINDOWS=1)
endif()
option(DM_VULKAN_NATIVE_RESOURCES "Build versioned host-native resource experiment" OFF)
if(DM_VULKAN_NATIVE_RESOURCES)
  if(NOT DM_VULKAN_SHARED_POOLS)
    message(FATAL_ERROR "DM_VULKAN_NATIVE_RESOURCES requires fixed shared Pools")
  endif()
  foreach(native_target dmhost_vulkan dmhost_android native_vulkan_test)
    target_compile_definitions(${native_target} PRIVATE DM_VULKAN_NATIVE_RESOURCES=1)
  endforeach()
endif()

option(DM_VULKAN_COLD_PHASE_REFERENCE "Inline complete Vulkan construction phases for comparison" OFF)
option(DM_VULKAN_POOL_PIN_REFERENCE "Acquire independent operation pins even for current recording owners" OFF)
if(DM_VULKAN_POOL_PIN_REFERENCE)
  foreach(pin_target dmhost_vulkan dmhost_android native_vulkan_test)
    target_compile_definitions(${pin_target} PRIVATE DM_VULKAN_POOL_PIN_REFERENCE=1)
  endforeach()
endif()
if(DM_VULKAN_COLD_PHASE_REFERENCE)
  foreach(phase_target dmhost_android native_vulkan_test)
    target_compile_definitions(${phase_target} PRIVATE DM_VULKAN_COLD_PHASE_REFERENCE=1)
  endforeach()
endif()
