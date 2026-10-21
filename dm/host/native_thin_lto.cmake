# Host compiler experiment only. No profiling runtime or renderer policy change.
include_guard(GLOBAL)
option(DM_NATIVE_THINLTO "Compile native host with Clang ThinLTO (experimental)" OFF)
function(dm_native_thinlto target)
  if(NOT DM_NATIVE_THINLTO)
    return()
  endif()
  if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR
      NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "DM_NATIVE_THINLTO requires Clang C/C++ and lld")
  endif()
  target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-flto=thin>")
  get_target_property(kind ${target} TYPE)
  if(NOT kind STREQUAL "OBJECT_LIBRARY")
    # Match the existing target optimization level in the LTO backend too.
    # Host GPU correctness fixtures are O1; the canonical Android host is O3.
    set(level 1)
    if(DEFINED DM_ANDROID_RUNTIME_OPT_LEVEL)
      set(level "${DM_ANDROID_RUNTIME_OPT_LEVEL}")
    endif()
    # Bound linker backends independently of the build system's -j setting.
    target_link_options(${target} PRIVATE -flto=thin "-O${level}" -fuse-ld=lld -Wl,--thinlto-jobs=2)
  endif()
endfunction()
