function(kataglyphis_configure_gtest_discovery test_target)
  if(NOT DEFINED KATAGLYPHIS_ENABLE_GTEST_DISCOVERY)
    set(KATAGLYPHIS_ENABLE_GTEST_DISCOVERY ON)
  endif()

  if(KATAGLYPHIS_ENABLE_GTEST_DISCOVERY)
    message(STATUS "Enabling gtest_discover_tests for ${test_target}.")
    # PRE_TEST keeps test discovery inside the ctest phase, which is safer for
    # Windows runtime-path handling than executing tests during build steps.
    gtest_discover_tests(
      ${test_target}
      DISCOVERY_TIMEOUT
      300
      DISCOVERY_MODE
      PRE_TEST)
  else()
    message(STATUS "KATAGLYPHIS_ENABLE_GTEST_DISCOVERY is OFF - skipping gtest_discover_tests for ${test_target}.")
  endif()
endfunction()

macro(kataglyphis_setup_vulkan_test_target TEST_TARGET)
  set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
  set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
  set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
  set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

  kataglyphis_collect_module_interfaces(VULKAN_MODULE_INTERFACE_FILES "${PROJECT_SRC_DIR}")
  kataglyphis_collect_module_interfaces(SHARED_MODULE_INTERFACE_FILES "${WORKING_DIRECTORY}Src/shared")

  set_target_properties(${TEST_TARGET} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

  set(ShaderIncludes
      -I ${SHADER_SRC_DIR}
      -I ${SHADER_SRC_DIR}common/
      -I ${SHADER_SRC_DIR}pbr/
      -I ${SHADER_SRC_DIR}pbr/brdf/
      -I ${SHADER_SRC_DIR}hostDevice/
      -I ${PROJECT_SRC_DIR}
      -I ${PROJECT_SRC_DIR}/renderer/
      -I ${PROJECT_SRC_DIR}/renderer/pushConstants/
      -I ${PROJECT_SRC_DIR}/scene/)

  string(REPLACE ";" " " ShaderIncludesString "${ShaderIncludes}")

  if(RUST_FEATURES)
    target_compile_definitions(${TEST_TARGET} PRIVATE USE_RUST=1)
  else()
    target_compile_definitions(${TEST_TARGET} PRIVATE USE_RUST=0)
  endif()

  set(VULKAN_RENDERER_CONFIG_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(VULKAN_RENDERER_CONFIG_MODULE_FILE "${VULKAN_RENDERER_CONFIG_MODULE_DIR}/VulkanRendererConfig.ixx")
  file(MAKE_DIRECTORY "${VULKAN_RENDERER_CONFIG_MODULE_DIR}")
  configure_file(${PROJECT_SRC_DIR}/VulkanRendererConfig.ixx.in "${VULKAN_RENDERER_CONFIG_MODULE_FILE}" @ONLY)
  set_source_files_properties("${VULKAN_RENDERER_CONFIG_MODULE_FILE}" PROPERTIES GENERATED TRUE)
  list(APPEND VULKAN_MODULE_INTERFACE_FILES "${VULKAN_RENDERER_CONFIG_MODULE_FILE}")

  target_sources(
    ${TEST_TARGET}
    PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES
           BASE_DIRS ${WORKING_DIRECTORY}Src/ ${VULKAN_RENDERER_CONFIG_MODULE_DIR}
           FILES ${VULKAN_MODULE_INTERFACE_FILES} ${SHARED_MODULE_INTERFACE_FILES})

  if(RUST_FEATURES)
    target_link_libraries(${TEST_TARGET} PUBLIC kataglyphis_rustprojecttemplate)
  endif()

  if(MSVC)
    target_compile_options(${TEST_TARGET} INTERFACE /w)
  else()
    target_compile_options(${TEST_TARGET} INTERFACE -w)
  endif()
endmacro()
