cmake_minimum_required(VERSION 3.20)
project(d2r_vulkan_validation C CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(alias_interval_test tests/alias_interval.c)
target_compile_options(alias_interval_test PRIVATE -UNDEBUG -Wall -Wextra)
add_executable(shared_import_android_test tests/shared_import_source.c)
target_compile_definitions(shared_import_android_test PRIVATE __ANDROID__=1 DM_VULKAN_TEST_EMBED=1)
target_compile_options(shared_import_android_test PRIVATE -UNDEBUG -Wall -Wextra)
add_executable(shared_import_no_provider_test tests/shared_import_source.c)
target_compile_definitions(shared_import_no_provider_test PRIVATE __ANDROID__=1 DM_VULKAN_TEST_EMBED=1 DM_SHARED_IMPORT_NO_PROVIDER=1)
target_compile_options(shared_import_no_provider_test PRIVATE -UNDEBUG -Wall -Wextra)
add_executable(shared_import_host_fixture_test tests/shared_import_source.c)
target_compile_definitions(shared_import_host_fixture_test PRIVATE DM_VULKAN_TEST_EMBED=1)
target_compile_options(shared_import_host_fixture_test PRIVATE -UNDEBUG -Wall -Wextra)
add_executable(vulkan_pace_test tests/pace.c nvn_vk_pace.c)
target_link_libraries(vulkan_pace_test PRIVATE m)
add_executable(astc_metadata_test tests/astc_metadata.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../tex/nvn_astc_metadata.cpp")
target_compile_options(astc_metadata_test PRIVATE -UNDEBUG -Wall -Wextra)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
set(CMAKE_C_STANDARD 11)
set(GFX "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(HOST "${GFX}/../host")
set(SHA "${GFX}/../third_party/runtime-codecs")
include("${CMAKE_CURRENT_SOURCE_DIR}/builtins.cmake")
set(DM_SUBPASS_TEST_HEADER "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_test_subpass_spv.h")
add_custom_command(OUTPUT "${DM_SUBPASS_TEST_HEADER}"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_test_subpass_spv.spv"
             "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_test_subpass_spv.json"
  COMMAND "${DM_VK_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/build_builtin.py"
          --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/subpass-input.frag"
          --output "${DM_SUBPASS_TEST_HEADER}" --symbol dm_test_subpass_spv
          --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build_builtin.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/subpass-input.frag"
  VERBATIM)
add_custom_target(dm_vulkan_subpass_test_shader DEPENDS "${DM_SUBPASS_TEST_HEADER}")
file(STRINGS "${GFX}/nvn/names.txt" names)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nvn_test_names.inc" "")
foreach(name ${names})
  file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/nvn_test_names.inc" "\"${name}\",\n")
endforeach()
file(GLOB BACKEND CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(vulkan_host_test tests/host.c ${BACKEND} "${GFX}/../third_party/volk/volk.c"
  "${GFX}/nvn/nvn_platform.c" "${GFX}/nvn/nvn_dirty.c" "${GFX}/nvn/nvn_tex.c"
  "${GFX}/tex/nvn_deswizzle.c" "${GFX}/tex/nvn_bcdec.c" "${GFX}/tex/nvn_astc_metadata.cpp" "${SHA}/sha256.c")
target_include_directories(vulkan_host_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${HOST}" "${SHA}" "${GFX}/nvn/include" ${Vulkan_INCLUDE_DIRS})
target_compile_definitions(vulkan_host_test PRIVATE _GNU_SOURCE DM_STRICT_HLE=1 DM_HEADLESS=1 DM_VULKAN_TEST_EMBED=1 MBEDTLS_CONFIG_FILE="dm_sha_config.h")
target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_SUBPASS_TEST_SHADERS=1 DM_DIRTY_RANGE_TEST_HOOKS=1 DM_VULKAN_SCAN_PAIR_ORACLE=1)
target_compile_options(vulkan_host_test PRIVATE -g -O1 -Wall -Wno-format -Wno-unused-function)
target_link_libraries(vulkan_host_test PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
# Owned fixture observes actual require calls, without production counters.
target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_VERTEX_REQUIRE_WRAP=1)
target_link_options(vulkan_host_test PRIVATE "-Wl,--wrap=vk_buffer_vertex_require")
add_dependencies(vulkan_host_test dm_vulkan_builtins)
add_dependencies(vulkan_host_test dm_vulkan_subpass_test_shader)

option(DM_VULKAN_SHARED_POOLS "Build fixed lazy whole shared-pool experiment" OFF)
if(DM_VULKAN_SHARED_POOLS)
  target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_SHARED_POOLS=1)
endif()

option(DM_GUEST_HEAP_GPU_TESTS "Link real memory SVC into owned host GPU fixture" OFF)
if(DM_GUEST_HEAP_GPU_TESTS)
  if(NOT DM_VULKAN_SHARED_POOLS)
    message(FATAL_ERROR "DM_GUEST_HEAP_GPU_TESTS requires DM_VULKAN_SHARED_POOLS")
  endif()
  target_sources(vulkan_host_test PRIVATE "${HOST}/hle_svc.c" tests/guest_heap_svc_stubs.c)
  target_compile_definitions(vulkan_host_test PRIVATE DM_GUEST_HEAP_GPU_TESTS=1)
endif()

# Observe completion batch calls in the owned GPU fixture, without production hooks.
target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_INVALIDATE_WRAP=1)
target_link_options(vulkan_host_test PRIVATE "-Wl,--wrap=vk_memory_invalidate_batch")

option(DM_VULKAN_SHARED_IMPORT_WINDOWS "Build experimental canonical shared imports (cold nonzero window bytes)" OFF)
if(DM_VULKAN_SHARED_IMPORT_WINDOWS)
  if(NOT DM_VULKAN_SHARED_POOLS)
    message(FATAL_ERROR "DM_VULKAN_SHARED_IMPORT_WINDOWS requires DM_VULKAN_SHARED_POOLS")
  endif()
  target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_SHARED_IMPORT_WINDOWS=1)
  target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_CPU_OBSERVE_WRAP=1)
  target_link_options(vulkan_host_test PRIVATE "-Wl,--wrap=nvn_dirty_cpu_observation_enable")
endif()
