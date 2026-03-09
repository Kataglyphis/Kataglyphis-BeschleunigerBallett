set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../)

if(NOT
   CMAKE_BUILD_TYPE
   STREQUAL
   "RelWithDebInfo")
  message(STATUS "Skipping perf tests: build type is '${CMAKE_BUILD_TYPE}', requires RelWithDebInfo.")
  return()
endif()

# update current positions
set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetShaderFilters.cmake)
include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetProjectFilters.cmake)
include(${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/cmake/filters/SetExternalLibsFilters.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)
include(${WORKING_DIRECTORY}Test/cmake/CommonTestBuild.cmake)

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

add_executable(${PERF_TEST_SUITE})

kataglyphis_setup_vulkan_test_target(${PERF_TEST_SUITE})

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

