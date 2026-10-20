# 使用构建机工具生成宿主着色器，不在手机上编译，也不依赖游戏着色器缓存。
# Host tools generate built-ins offline, independently of the game shader cache.
find_program(DM_VK_GLSLANG glslangValidator REQUIRED NO_CMAKE_FIND_ROOT_PATH)
find_program(DM_VK_SPIRV_VAL spirv-val REQUIRED NO_CMAKE_FIND_ROOT_PATH)
find_program(DM_VK_PYTHON python3 REQUIRED NO_CMAKE_FIND_ROOT_PATH)
set(DM_VK_BUILTIN_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(DM_VK_BRIGHT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_bright_spv.h")
add_custom_command(OUTPUT "${DM_VK_BRIGHT_HEADER}"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_bright_spv.spv"
             "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_bright_spv.json"
  COMMAND "${DM_VK_PYTHON}" "${DM_VK_BUILTIN_DIR}/build_builtin.py"
          --source "${DM_VK_BUILTIN_DIR}/shaders/bright.comp"
          --output "${DM_VK_BRIGHT_HEADER}"
          --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
  DEPENDS "${DM_VK_BUILTIN_DIR}/build_builtin.py"
          "${DM_VK_BUILTIN_DIR}/shaders/bright.comp"
  VERBATIM)
set(DM_VK_EXTRAP_HEADERS)
foreach(stage vert frag)
  set(header "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_extrap_${stage}_spv.h")
  add_custom_command(OUTPUT "${header}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_extrap_${stage}_spv.spv"
               "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_extrap_${stage}_spv.json"
    COMMAND "${DM_VK_PYTHON}" "${DM_VK_BUILTIN_DIR}/build_builtin.py"
            --source "${DM_VK_BUILTIN_DIR}/shaders/extrap.${stage}"
            --output "${header}" --symbol "dm_extrap_${stage}_spv"
            --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
    DEPENDS "${DM_VK_BUILTIN_DIR}/build_builtin.py"
            "${DM_VK_BUILTIN_DIR}/shaders/extrap.${stage}"
    VERBATIM)
  list(APPEND DM_VK_EXTRAP_HEADERS "${header}")
endforeach()
foreach(kind masked mask)
  set(header "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_extrap_${kind}_spv.h")
  if(kind STREQUAL masked)
    set(source "${DM_VK_BUILTIN_DIR}/shaders/extrap.frag")
    set(define --define DM_RX_MASK=1)
  else()
    set(source "${DM_VK_BUILTIN_DIR}/shaders/extrap-mask.frag")
    set(define)
  endif()
  add_custom_command(OUTPUT "${header}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_extrap_${kind}_spv.spv"
               "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_extrap_${kind}_spv.json"
    COMMAND "${DM_VK_PYTHON}" "${DM_VK_BUILTIN_DIR}/build_builtin.py"
            --source "${source}" --output "${header}" --symbol "dm_extrap_${kind}_spv" ${define}
            --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
    DEPENDS "${DM_VK_BUILTIN_DIR}/build_builtin.py" "${source}"
    VERBATIM)
  list(APPEND DM_VK_EXTRAP_HEADERS "${header}")
endforeach()
set(DM_VK_NOFXAA_HEADER "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_nofxaa_spv.h")
add_custom_command(OUTPUT "${DM_VK_NOFXAA_HEADER}"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_nofxaa_spv.spv"
             "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_nofxaa_spv.json"
  COMMAND "${DM_VK_PYTHON}" "${DM_VK_BUILTIN_DIR}/build_builtin.py"
          --source "${DM_VK_BUILTIN_DIR}/shaders/nofxaa.frag"
          --output "${DM_VK_NOFXAA_HEADER}" --symbol "dm_nofxaa_spv"
          --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
  DEPENDS "${DM_VK_BUILTIN_DIR}/build_builtin.py"
          "${DM_VK_BUILTIN_DIR}/shaders/nofxaa.frag"
  VERBATIM)
set(DM_VK_BLOOM_QUARTER_HEADERS)
foreach(stage vert frag)
  set(header "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_bloom_quarter_${stage}_spv.h")
  add_custom_command(OUTPUT "${header}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_bloom_quarter_${stage}_spv.spv"
               "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_bloom_quarter_${stage}_spv.json"
    COMMAND "${DM_VK_PYTHON}" "${DM_VK_BUILTIN_DIR}/build_builtin.py"
            --source "${DM_VK_BUILTIN_DIR}/shaders/bloom-quarter.${stage}"
            --output "${header}" --symbol "dm_bloom_quarter_${stage}_spv"
            --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
    DEPENDS "${DM_VK_BUILTIN_DIR}/build_builtin.py"
            "${DM_VK_BUILTIN_DIR}/shaders/bloom-quarter.${stage}"
    VERBATIM)
  list(APPEND DM_VK_BLOOM_QUARTER_HEADERS "${header}")
endforeach()
set(DM_VK_PRESENT_HEADERS)
foreach(stage vert frag)
  set(header "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_present_${stage}_spv.h")
  add_custom_command(OUTPUT "${header}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_present_${stage}_spv.spv"
               "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_present_${stage}_spv.json"
    COMMAND "${DM_VK_PYTHON}" "${DM_VK_BUILTIN_DIR}/build_builtin.py"
            --source "${DM_VK_BUILTIN_DIR}/shaders/present.${stage}"
            --output "${header}" --symbol "dm_present_${stage}_spv"
            --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
    DEPENDS "${DM_VK_BUILTIN_DIR}/build_builtin.py"
            "${DM_VK_BUILTIN_DIR}/shaders/present.${stage}"
    VERBATIM)
  list(APPEND DM_VK_PRESENT_HEADERS "${header}")
endforeach()
set(DM_VK_EVENT_ADD_HEADER "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_event_add_spv.h")
add_custom_command(OUTPUT "${DM_VK_EVENT_ADD_HEADER}"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_event_add_spv.spv"
             "${CMAKE_CURRENT_BINARY_DIR}/nvn_vk_event_add_spv.json"
  COMMAND "${DM_VK_PYTHON}" "${DM_VK_BUILTIN_DIR}/build_builtin.py"
          --source "${DM_VK_BUILTIN_DIR}/shaders/event-add.comp"
          --output "${DM_VK_EVENT_ADD_HEADER}" --symbol dm_event_add_spv
          --glslang "${DM_VK_GLSLANG}" --validator "${DM_VK_SPIRV_VAL}"
  DEPENDS "${DM_VK_BUILTIN_DIR}/build_builtin.py" "${DM_VK_BUILTIN_DIR}/shaders/event-add.comp"
  VERBATIM)
add_custom_target(dm_vulkan_builtins DEPENDS "${DM_VK_BRIGHT_HEADER}" ${DM_VK_EXTRAP_HEADERS}
                  "${DM_VK_NOFXAA_HEADER}" ${DM_VK_BLOOM_QUARTER_HEADERS} ${DM_VK_PRESENT_HEADERS}
                  "${DM_VK_EVENT_ADD_HEADER}")
