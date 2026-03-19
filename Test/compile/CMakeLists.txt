include(GoogleTest)

if(NOT
   CMAKE_BUILD_TYPE
   STREQUAL
   "Debug")
  message(STATUS "Skipping compile tests: build type is '${CMAKE_BUILD_TYPE}', requires Debug.")
  return()
endif()

set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../)

# update current positions
set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

include(${WORKING_DIRECTORY}Test/cmake/CommonTestBuild.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetShaderFilters.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetProjectFilters.cmake)
include(${PROJECT_SRC_DIR}/cmake/filters/SetExternalLibsFilters.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)

file(GLOB_RECURSE RENDERER_COMPILE_TEST_FILTER "*.cpp")

if(MYPROJECT_CMAKE_DEBUG_LOG)
  message(STATUS "Render compile test suite files are: ${RENDERER_COMPILE_TEST_FILTER}")
else()
  message(VERBOSE "Render compile test suite files are: ${RENDERER_COMPILE_TEST_FILTER}")
endif()

set(COMPILE_TEST_SUITE compileTestSuite)

kataglyphis_collect_module_interfaces(VULKAN_MODULE_INTERFACE_FILES "${PROJECT_SRC_DIR}")
kataglyphis_collect_module_interfaces(SHARED_MODULE_INTERFACE_FILES "${WORKING_DIRECTORY}Src/shared")

file(GLOB_RECURSE VULKANRENDERER_SOURCES "${PROJECT_SRC_DIR}/*.cpp")

# Specify the file to exclude
list(REMOVE_ITEM VULKANRENDERER_SOURCES "${PROJECT_SRC_DIR}/Main.cpp")

file(GLOB_RECURSE SHADER_HEADERS "${SHADER_SRC_DIR}/*.hpp")

file(GLOB_RECURSE VULKANRENDERER_HEADERS "${PROJECT_SRC_DIR}/*.hpp")

add_executable(${COMPILE_TEST_SUITE})
set_target_properties(${COMPILE_TEST_SUITE} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

set(ShaderIncludes
    -I
    ${SHADER_SRC_DIR}
    -I
    ${SHADER_SRC_DIR}common/
    -I
    ${SHADER_SRC_DIR}pbr/
    -I
    ${SHADER_SRC_DIR}pbr/brdf/
    -I
    ${SHADER_SRC_DIR}hostDevice/
    -I
    ${PROJECT_SRC_DIR}
    -I
    ${PROJECT_SRC_DIR}/renderer/
    -I
    ${PROJECT_SRC_DIR}/renderer/pushConstants/
    -I
    ${PROJECT_SRC_DIR}/scene/)

# Join the paths into a single space-separated string
string(
  REPLACE ";"
          " "
          ShaderIncludesString
          "${ShaderIncludes}")

if(MYPROJECT_CMAKE_DEBUG_LOG)
  message(STATUS "Shader include string: ${ShaderIncludesString}")
else()
  message(VERBOSE "Shader include string: ${ShaderIncludesString}")
endif()

if(RUST_FEATURES)
  target_compile_definitions(${COMPILE_TEST_SUITE} PRIVATE USE_RUST=1)
else()
  target_compile_definitions(${COMPILE_TEST_SUITE} PRIVATE USE_RUST=0)
endif()

kataglyphis_apply_runtime_compile_definitions(
  ${COMPILE_TEST_SUITE}
  RESOURCE_PATH_NON_MSVC
  "/../../../Resources/"
  RESOURCE_PATH_MSVC
  "/../../Resources/"
  INCLUDE_PATH_NON_MSVC
  "/../../../Src/GraphicsEngineVulkan/"
  INCLUDE_PATH_MSVC
  "/../../Src/GraphicsEngineVulkan/"
  IMGUI_FONTS_PATH_NON_MSVC
  "/../../../ExternalLib/IMGUI/misc/fonts/"
  IMGUI_FONTS_PATH_MSVC
  "/../../ExternalLib/IMGUI/misc/fonts/"
  SHADER_INCLUDES_STRING
  "${ShaderIncludesString}")

set(VULKAN_RENDERER_CONFIG_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(VULKAN_RENDERER_CONFIG_MODULE_FILE "${VULKAN_RENDERER_CONFIG_MODULE_DIR}/VulkanRendererConfig.ixx")
file(MAKE_DIRECTORY "${VULKAN_RENDERER_CONFIG_MODULE_DIR}")
configure_file(${PROJECT_SRC_DIR}/VulkanRendererConfig.ixx.in "${VULKAN_RENDERER_CONFIG_MODULE_FILE}" @ONLY)
set_source_files_properties("${VULKAN_RENDERER_CONFIG_MODULE_FILE}" PROPERTIES GENERATED TRUE)
list(APPEND VULKAN_MODULE_INTERFACE_FILES "${VULKAN_RENDERER_CONFIG_MODULE_FILE}")

target_sources(
  ${COMPILE_TEST_SUITE}
  PUBLIC ${RENDERER_COMPILE_TEST_FILTER}
         ${RASTER_SHADER_FILTER}
         ${RAYTRACING_SHADER_FILTER}
         ${COMMON_SHADER_FILTER}
         ${POST_SHADER_FILTER}
         ${BRDF_SHADER_FILTER}
         ${PBR_SHADER_FILTER}
         ${PATH_TRACING_SHADER_FILTER}
         ${VULKANRENDERER_SOURCES}
         # this is great; no CPPCHECK,CLANG_TIDY here
         $<TARGET_OBJECTS:IMGUI>
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${PROJECT_INCLUDE_DIR}
         ${SHADER_SRC_DIR}
         FILES
         ${VULKANRENDERER_HEADERS}
         ${SHADER_HEADERS})

target_sources(
  ${COMPILE_TEST_SUITE}
  PUBLIC FILE_SET
         CXX_MODULES
         TYPE
         CXX_MODULES
         BASE_DIRS
         ${WORKING_DIRECTORY}Src/
         ${VULKAN_RENDERER_CONFIG_MODULE_DIR}
         FILES
         ${VULKAN_MODULE_INTERFACE_FILES}
         ${SHARED_MODULE_INTERFACE_FILES})

target_include_directories(${COMPILE_TEST_SUITE} PRIVATE ${Vulkan_INCLUDE_DIRS})

target_compile_definitions(${COMPILE_TEST_SUITE} PRIVATE VULKAN_HPP_NO_EXCEPTIONS VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

target_link_libraries(
  ${COMPILE_TEST_SUITE}
  PUBLIC ${CMAKE_DL_LIBS}
         Threads::Threads
         Vulkan::Vulkan
         glfw
         imgui
         stb
         glm
         tinyobjloader
         vma
         ktx
         # enable compiler warnings
         myproject_warnings
         # enable sanitizers
         myproject_options
  PRIVATE gtest_main
          gtest
          GSL
          spdlog)

kataglyphis_configure_gtest_discovery(${COMPILE_TEST_SUITE})

if(RUST_FEATURES)
  target_link_libraries(${COMPILE_TEST_SUITE} PUBLIC kataglyphis_rustprojecttemplate)
endif()

# disable all warnings for our test suite
if(MSVC)
  target_compile_options(${COMPILE_TEST_SUITE} INTERFACE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(${COMPILE_TEST_SUITE} INTERFACE -w)
else()
  target_compile_options(${COMPILE_TEST_SUITE} INTERFACE -w)
endif()
