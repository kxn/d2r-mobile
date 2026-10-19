cmake_minimum_required(VERSION 3.20)
project(d2r_vulkan_validation C CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(vulkan_pace_test tests/pace.c nvn_vk_pace.c)
target_link_libraries(vulkan_pace_test PRIVATE m)
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
  "${GFX}/tex/nvn_deswizzle.c" "${GFX}/tex/nvn_bcdec.c" "${SHA}/sha256.c")
target_include_directories(vulkan_host_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${HOST}" "${SHA}" "${GFX}/nvn/include" ${Vulkan_INCLUDE_DIRS})
target_compile_definitions(vulkan_host_test PRIVATE _GNU_SOURCE DM_STRICT_HLE=1 DM_HEADLESS=1 DM_VULKAN_TEST_EMBED=1 MBEDTLS_CONFIG_FILE="dm_sha_config.h")
target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_SUBPASS_TEST_SHADERS=1)
target_compile_options(vulkan_host_test PRIVATE -g -O1 -Wall -Wno-format -Wno-unused-function)
target_link_libraries(vulkan_host_test PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
# Owned fixture observes actual require calls, without production counters.
target_compile_definitions(vulkan_host_test PRIVATE DM_VULKAN_VERTEX_REQUIRE_WRAP=1)
target_link_options(vulkan_host_test PRIVATE "-Wl,--wrap=vk_buffer_vertex_require")
add_dependencies(vulkan_host_test dm_vulkan_builtins)
add_dependencies(vulkan_host_test dm_vulkan_subpass_test_shader)
