# compile glslc shaders
# source: https://www.reddit.com/r/vulkan/comments/kbaxlz/what_is_your_workflow_when_compiling_shader_files/

message("The shader inlcudes are the following: ${ShaderIncludes}")

set(GLSLC_TARGET_ENV "vulkan${VULKAN_VERSION_MAJOR}.${VULKAN_VERSION_MINOR}")
message("Shader target environment is: ${GLSLC_TARGET_ENV}")

function(add_shader TARGET SHADER)
  find_program(GLSLC glslc)

  set(current-shader-path ${SHADER}) #${CMAKE_CURRENT_SOURCE_DIR}/
  message("Current shader path is: ${SHADER}")
  get_filename_component(a_dir "${current-shader-path}" PATH)
  get_filename_component(a_last_dir "${current-shader-path}" NAME)

  set(current-output-path ${a_dir}/spv/${a_last_dir}.spv)
  # message(STATUS "${current-output-path}")

  get_filename_component(current-output-dir ${current-output-path} DIRECTORY)
  file(MAKE_DIRECTORY ${current-output-dir})

  add_custom_command(
    OUTPUT ${current-output-path}
    COMMAND ${GLSLC} --target-env=${GLSLC_TARGET_ENV} ${current-shader-path} -o ${current-output-path} ${ShaderIncludes}
    DEPENDS ${current-shader-path}
    IMPLICIT_DEPENDS CXX ${current-shader-path}
    VERBATIM)

  message("Compiled shader: " "${current-shader-path}")

  # Make sure our build depends on this output.
  set_source_files_properties(${current-output-path} PROPERTIES GENERATED TRUE)
  target_sources(${TARGET} PRIVATE ${current-output-path})
endfunction(add_shader)
