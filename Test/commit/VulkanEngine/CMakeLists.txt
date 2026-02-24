include(GoogleTest)

# all testing related stuff
set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../)

# update current positions
set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

include(${PROJECT_SRC_DIR}cmake/filters/SetShaderFilters.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetProjectFilters.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetExternalLibsFilters.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)

set(COMMIT_TEST_SUITE commitTestSuite)

kataglyphis_collect_module_interfaces(VULKAN_MODULE_INTERFACE_FILES "${PROJECT_SRC_DIR}")

file(GLOB_RECURSE RENDERER_COMMIT_TEST_FILTER "*.cpp")

message("Render commit test filter is: ${RENDERER_COMMIT_TEST_FILTER}")

file(GLOB_RECURSE VULKANRENDERER_SOURCES "${PROJECT_SRC_DIR}/*.cpp")

# Specify the file to exclude
list(REMOVE_ITEM VULKANRENDERER_SOURCES "${PROJECT_SRC_DIR}/Main.cpp")

file(GLOB_RECURSE SHADER_HEADERS "${SHADER_SRC_DIR}/*.hpp")

file(GLOB_RECURSE VULKANRENDERER_HEADERS "${PROJECT_SRC_DIR}/*.hpp")

# if(RUST_FEATURES)
#   corrosion_import_crate(MANIFEST_PATH ${PROJECT_SRC_DIR}rusty_code/Cargo.toml)
#   corrosion_add_cxxbridge(rusty_bridge_commit CRATE rusty_code FILES lib.rs)
# endif()

add_executable(${COMMIT_TEST_SUITE})

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

message("The shader include string is the following: ${ShaderIncludesString}")

if(RUST_FEATURES)
  target_compile_definitions(${COMMIT_TEST_SUITE} PRIVATE USE_RUST=1)
else()
  target_compile_definitions(${COMMIT_TEST_SUITE} PRIVATE USE_RUST=0)
endif()

kataglyphis_apply_runtime_compile_definitions(
  ${COMMIT_TEST_SUITE}
  RESOURCE_PATH_NON_MSVC "/../../../../Resources/"
  RESOURCE_PATH_MSVC "/../../../../Resources/"
  INCLUDE_PATH_NON_MSVC "/../../../../Src/GraphicsEngineVulkan/"
  INCLUDE_PATH_MSVC "/../../../../Src/GraphicsEngineVulkan/"
  IMGUI_FONTS_PATH_NON_MSVC "/../../../../ExternalLib/IMGUI/misc/fonts/"
  IMGUI_FONTS_PATH_MSVC "/../../../../ExternalLib/IMGUI/misc/fonts/"
  SHADER_INCLUDES_STRING "${ShaderIncludesString}")

configure_file(${PROJECT_SRC_DIR}/VulkanRendererConfig.hpp.in "${PROJECT_SRC_DIR}/renderer/VulkanRendererConfig.hpp")

target_sources(
  ${COMMIT_TEST_SUITE}
  PUBLIC ${RENDERER_COMMIT_TEST_FILTER}
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
  ${COMMIT_TEST_SUITE}
  PUBLIC FILE_SET
         CXX_MODULES
         TYPE
         CXX_MODULES
         BASE_DIRS
         ${PROJECT_INCLUDE_DIR}
         FILES
         ${VULKAN_MODULE_INTERFACE_FILES})

set_target_properties(${COMMIT_TEST_SUITE} PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_include_directories(${COMMIT_TEST_SUITE} PRIVATE ${Vulkan_INCLUDE_DIRS})

target_link_libraries(
  ${COMMIT_TEST_SUITE}
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
  PRIVATE gtest
          gtest_main
          GSL
          spdlog)

if(NOT WINDOWS_CI)
  message(STATUS "WINDOWS_CI is OFF or not defined.")
  gtest_discover_tests(${COMMIT_TEST_SUITE} DISCOVERY_TIMEOUT 300)
else()
  message(STATUS "WINDOWS_CI is OFF or not defined.")
endif()

if(RUST_FEATURES)
  target_link_libraries(${COMMIT_TEST_SUITE} PUBLIC rusty_code)
endif()

# disable all warnings for our test suite
if(MSVC)
  target_compile_options(${COMMIT_TEST_SUITE} INTERFACE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(${COMMIT_TEST_SUITE} INTERFACE -w)
else()
  target_compile_options(${COMMIT_TEST_SUITE} INTERFACE -w)
endif()
