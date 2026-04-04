include(CMakeParseArguments)

# Collects C++20 module interface files (.ixx) from a directory.
# Sets two variables in parent scope:
#   ${out_files_var} - List of absolute paths to module interface files
#   ${out_base_dir_var} - The canonical absolute base directory (for use in FILE_SET BASE_DIRS)
#
# Usage:
#   kataglyphis_collect_module_interfaces(MY_FILES MY_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules")
#
function(kataglyphis_collect_module_interfaces out_files_var out_base_dir_var base_dir)
  # Use file(REAL_PATH) to get a truly canonical absolute path
  # This is critical for Windows where clang-cl module scanning requires exact path matching
  file(REAL_PATH "${base_dir}" _real_base_dir)
  
  file(
    GLOB_RECURSE _module_interface_relative_files
    RELATIVE "${_real_base_dir}"
    "${_real_base_dir}/*.ixx")
  list(SORT _module_interface_relative_files)

  # Build absolute paths for each file using the same canonical base
  set(_absolute_files "")
  foreach(_rel_file IN LISTS _module_interface_relative_files)
    file(REAL_PATH "${_real_base_dir}/${_rel_file}" _abs_file)
    list(APPEND _absolute_files "${_abs_file}")
  endforeach()

  set(${out_files_var}
      "${_absolute_files}"
      PARENT_SCOPE)
  set(${out_base_dir_var}
      "${_real_base_dir}"
      PARENT_SCOPE)
endfunction()

function(kataglyphis_apply_runtime_compile_definitions target)
  set(options)
  set(oneValueArgs
      RESOURCE_PATH_NON_MSVC
      RESOURCE_PATH_MSVC
      INCLUDE_PATH_NON_MSVC
      INCLUDE_PATH_MSVC
      IMGUI_FONTS_PATH_NON_MSVC
      IMGUI_FONTS_PATH_MSVC
      SHADER_INCLUDES_STRING)
  set(multiValueArgs)
  cmake_parse_arguments(
    KAT
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN})

  set(_compile_defs)
  if(MSVC)
    if(KAT_RESOURCE_PATH_MSVC)
      list(APPEND _compile_defs RELATIVE_RESOURCE_PATH="${KAT_RESOURCE_PATH_MSVC}")
    endif()
    if(KAT_INCLUDE_PATH_MSVC)
      list(APPEND _compile_defs RELATIVE_INCLUDE_PATH="${KAT_INCLUDE_PATH_MSVC}")
    endif()
    if(KAT_IMGUI_FONTS_PATH_MSVC)
      list(APPEND _compile_defs RELATIVE_IMGUI_FONTS_PATH="${KAT_IMGUI_FONTS_PATH_MSVC}")
    endif()
  else()
    if(KAT_RESOURCE_PATH_NON_MSVC)
      list(APPEND _compile_defs RELATIVE_RESOURCE_PATH="${KAT_RESOURCE_PATH_NON_MSVC}")
    endif()
    if(KAT_INCLUDE_PATH_NON_MSVC)
      list(APPEND _compile_defs RELATIVE_INCLUDE_PATH="${KAT_INCLUDE_PATH_NON_MSVC}")
    endif()
    if(KAT_IMGUI_FONTS_PATH_NON_MSVC)
      list(APPEND _compile_defs RELATIVE_IMGUI_FONTS_PATH="${KAT_IMGUI_FONTS_PATH_NON_MSVC}")
    endif()
  endif()

  if(KAT_SHADER_INCLUDES_STRING)
    list(APPEND _compile_defs ShaderIncludesString="${KAT_SHADER_INCLUDES_STRING}")
  endif()

  if(_compile_defs)
    target_compile_definitions(${target} PRIVATE ${_compile_defs})
  endif()
endfunction()
