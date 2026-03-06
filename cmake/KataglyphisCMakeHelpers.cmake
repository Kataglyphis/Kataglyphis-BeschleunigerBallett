include(CMakeParseArguments)

function(kataglyphis_collect_module_interfaces out_var base_dir)
  string(REGEX REPLACE "/+$" "" base_dir "${base_dir}")
  file(
    GLOB_RECURSE _module_interface_relative_files
    RELATIVE "${base_dir}"
    "${base_dir}/*.ixx")
  list(SORT _module_interface_relative_files)

  set(_module_interface_files "${_module_interface_relative_files}")
  list(TRANSFORM _module_interface_files PREPEND "${base_dir}/")

  set(${out_var}
      "${_module_interface_files}"
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
