macro(kataglyphis_setup_engine_backend TARGET_NAME ENGINE_NAME UPPER_ENGINE_NAME)
  set_target_properties(${TARGET_NAME} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

  if(RUST_FEATURES)
    target_compile_definitions(${TARGET_NAME} PRIVATE USE_RUST=1)
  else()
    target_compile_definitions(${TARGET_NAME} PRIVATE USE_RUST=0)
  endif()

  set(${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_FILE "${${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_DIR}/${ENGINE_NAME}RendererConfig.ixx")
  file(MAKE_DIRECTORY "${${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_DIR}")
  configure_file(${ENGINE_NAME}RendererConfig.ixx.in "${${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_FILE}" @ONLY)
  set_source_files_properties("${${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_FILE}" PROPERTIES GENERATED TRUE)
  list(APPEND ${UPPER_ENGINE_NAME}_MODULE_INTERFACE_FILES "${${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_FILE}")

  target_sources(
    ${TARGET_NAME}
    PUBLIC FILE_SET
           CXX_MODULES
           TYPE
           CXX_MODULES
           BASE_DIRS
           ${CMAKE_CURRENT_SOURCE_DIR}/..
           ${${UPPER_ENGINE_NAME}_RENDERER_CONFIG_MODULE_DIR}
           FILES
           ${${UPPER_ENGINE_NAME}_MODULE_INTERFACE_FILES}
           ${SHARED_MODULE_INTERFACE_FILES})

  # Enable ASan and UBSan in Debug mode on Linux
  target_compile_options(${TARGET_NAME} PRIVATE $<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>>:-fsanitize=address>
                                                 $<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>>:-fno-omit-frame-pointer>)
  target_link_options(${TARGET_NAME} PRIVATE $<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>>:-fsanitize=address>)
endmacro()

macro(kataglyphis_setup_engine_dirs_and_filters)
  set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
  set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
  set(EXTERNAL_LIB_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../ExternalLib/)
  set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/Shaders/)

  include(cmake/filters/SetShaderFilters.cmake)
  include(cmake/filters/SetProjectFilters.cmake)
  include(cmake/SetSourceGroups.cmake)
  include(cmake/filters/SetExternalLibsFilters.cmake)
  include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)
endmacro()
