include(GoogleTest)

# all testing related stuff
set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../)

# update current positions
set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineVulkan/)
set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

include(${WORKING_DIRECTORY}Test/cmake/CommonTestBuild.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetShaderFilters.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetProjectFilters.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetExternalLibsFilters.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)

set(COMMIT_TEST_SUITE commitTestSuite)


file(GLOB_RECURSE RENDERER_COMMIT_TEST_FILTER "*.cpp")

if(MYPROJECT_CMAKE_DEBUG_LOG)
  message(STATUS "Render commit test filter is: ${RENDERER_COMMIT_TEST_FILTER}")
else()
  message(VERBOSE "Render commit test filter is: ${RENDERER_COMMIT_TEST_FILTER}")
endif()

file(GLOB_RECURSE VULKANRENDERER_SOURCES "${PROJECT_SRC_DIR}/*.cpp")

# Specify the file to exclude
list(REMOVE_ITEM VULKANRENDERER_SOURCES "${PROJECT_SRC_DIR}/Main.cpp")

file(GLOB_RECURSE SHADER_HEADERS "${SHADER_SRC_DIR}/*.hpp")

file(GLOB_RECURSE VULKANRENDERER_HEADERS "${PROJECT_SRC_DIR}/*.hpp")

add_executable(${COMMIT_TEST_SUITE})

kataglyphis_setup_vulkan_test_target(${COMMIT_TEST_SUITE})

if(MYPROJECT_CMAKE_DEBUG_LOG)
  message(STATUS "Shader include string: ${ShaderIncludesString}")
else()
  message(VERBOSE "Shader include string: ${ShaderIncludesString}")
endif()

kataglyphis_apply_runtime_compile_definitions(
  ${COMMIT_TEST_SUITE}
  RESOURCE_PATH_NON_MSVC
  "/../../../../Resources/"
  RESOURCE_PATH_MSVC
  "/../../../../Resources/"
  INCLUDE_PATH_NON_MSVC
  "/../../../../Src/GraphicsEngineVulkan/"
  INCLUDE_PATH_MSVC
  "/../../../../Src/GraphicsEngineVulkan/"
  IMGUI_FONTS_PATH_NON_MSVC
  "/../../../../ExternalLib/IMGUI/misc/fonts/"
  IMGUI_FONTS_PATH_MSVC
  "/../../../../ExternalLib/IMGUI/misc/fonts/"
  SHADER_INCLUDES_STRING
  "${ShaderIncludesString}")

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

kataglyphis_configure_gtest_discovery(${COMMIT_TEST_SUITE})
