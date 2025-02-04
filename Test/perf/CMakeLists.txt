set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../)

# update current positions
set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetShaderFilters.cmake)
include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetProjectFilters.cmake)
include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetExternalLibsFilters.cmake)

include(cmake/SetTestFilters.cmake)

set(PERF_TEST_SUITE perfTestSuite)

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
  target_compile_definitions(${PROJECT_NAME} PRIVATE USE_RUST=1)
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE USE_RUST=0)
endif()

if(NOT MSVC)
  target_compile_definitions(
    ${PERF_TEST_SUITE}
    PRIVATE RELATIVE_RESOURCE_PATH="/../../Resources/"
            RELATIVE_INCLUDE_PATH="/../../Src/GraphicsEngineVulkan/"
            RELATIVE_IMGUI_FONTS_PATH="/../../ExternalLib/IMGUI/misc/fonts/"
            ShaderIncludesString="${ShaderIncludesString}")
else()
  target_compile_definitions(
    ${PERF_TEST_SUITE}
    PRIVATE RELATIVE_RESOURCE_PATH="/../../Resources/"
            RELATIVE_INCLUDE_PATH="/../../Src/GraphicsEngineVulkan/"
            RELATIVE_IMGUI_FONTS_PATH="/../../ExternalLib/IMGUI/misc/fonts/"
            ShaderIncludesString="${ShaderIncludesString}")
endif()

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
