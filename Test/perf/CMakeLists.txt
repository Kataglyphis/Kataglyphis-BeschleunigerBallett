set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../)

# update current positions
set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetShaderFilters.cmake)
include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetProjectFilters.cmake)
include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetExternalLibsFilters.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)

include(cmake/SetTestFilters.cmake)

set(PERF_TEST_SUITE perfTestSuite)

kataglyphis_collect_module_interfaces(VULKAN_MODULE_INTERFACE_FILES "${PROJECT_SRC_DIR}")
kataglyphis_collect_module_interfaces(SHARED_MODULE_INTERFACE_FILES "${WORKING_DIRECTORY}Src/shared")

file(
  GLOB_RECURSE VULKANRENDERER_SOURCES
  RELATIVE ${PROJECT_SRC_DIR}
  "*.cpp")

file(
  GLOB_RECURSE SHADER_HEADERS
  RELATIVE ${SHADER_SRC_DIR}
  "*.hpp")

file(
  GLOB_RECURSE VULKANRENDERER_HEADERS
  RELATIVE ${PROJECT_SRC_DIR}
  "*.hpp")

# if(RUST_FEATURES)
#   corrosion_import_crate(MANIFEST_PATH ${PROJECT_SRC_DIR}rusty_code/Cargo.toml)
#   corrosion_add_cxxbridge(rusty_bridge_perf CRATE rusty_code FILES lib.rs)
# endif()

add_executable(${PERF_TEST_SUITE})
set_target_properties(${PERF_TEST_SUITE} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

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
  target_compile_definitions(${PERF_TEST_SUITE} PRIVATE USE_RUST=1)
else()
  target_compile_definitions(${PERF_TEST_SUITE} PRIVATE USE_RUST=0)
endif()

kataglyphis_apply_runtime_compile_definitions(
  ${PERF_TEST_SUITE}
  RESOURCE_PATH_NON_MSVC
  "/../../Resources/"
  RESOURCE_PATH_MSVC
  "/../../Resources/"
  INCLUDE_PATH_NON_MSVC
  "/../../Src/GraphicsEngineVulkan/"
  INCLUDE_PATH_MSVC
  "/../../Src/GraphicsEngineVulkan/"
  IMGUI_FONTS_PATH_NON_MSVC
  "/../../ExternalLib/IMGUI/misc/fonts/"
  IMGUI_FONTS_PATH_MSVC
  "/../../ExternalLib/IMGUI/misc/fonts/"
  SHADER_INCLUDES_STRING
  "${ShaderIncludesString}")

configure_file(${PROJECT_SRC_DIR}/VulkanRendererConfig.hpp.in "${PROJECT_SRC_DIR}/renderer/VulkanRendererConfig.hpp")

target_sources(
  ${PERF_TEST_SUITE}
  PUBLIC ${RENDERER_PERF_TEST_FILTER}
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
  ${PERF_TEST_SUITE}
  PUBLIC FILE_SET
         CXX_MODULES
         TYPE
         CXX_MODULES
         BASE_DIRS
         ${WORKING_DIRECTORY}Src/
         FILES
         ${VULKAN_MODULE_INTERFACE_FILES}
         ${SHARED_MODULE_INTERFACE_FILES})

target_include_directories(${PERF_TEST_SUITE} PRIVATE ${Vulkan_INCLUDE_DIRS})

target_link_libraries(
  ${PERF_TEST_SUITE}
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
         myproject_options
         myproject_warnings
  PRIVATE benchmark::benchmark
          benchmark::benchmark_main
          GSL
          spdlog)

if(RUST_FEATURES)
  target_link_libraries(${PERF_TEST_SUITE} PUBLIC rusty_code)
endif()

# disable all warnings for our test suite
if(MSVC)
  target_compile_options(${PERF_TEST_SUITE} INTERFACE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(${PERF_TEST_SUITE} INTERFACE -w)
else()
  target_compile_options(${PERF_TEST_SUITE} INTERFACE -w)
endif()
