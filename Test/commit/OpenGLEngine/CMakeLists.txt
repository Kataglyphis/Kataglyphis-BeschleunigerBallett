# all testing related stuff

set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../)

# update current positions
set(PROJECT_SRC_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineOpenGL/)
set(PROJECT_INCLUDE_DIR ${WORKING_DIRECTORY}Src/GraphicsEngineOpenGL/)
set(EXTERNAL_LIB_SRC_DIR ${WORKING_DIRECTORY}ExternalLib/)
set(SHADER_SRC_DIR ${WORKING_DIRECTORY}Resources/Shaders/)

include(${PROJECT_SRC_DIR}cmake/filters/SetShaderFilters.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetProjectFilters.cmake)
include(${PROJECT_SRC_DIR}cmake/filters/SetExternalLibsFilters.cmake)

set(COMMIT_TEST_SUITE_OPENGL commitTestSuiteOpenGL)

file(GLOB_RECURSE OPENGLRENDERER_SOURCES "${PROJECT_SRC_DIR}/*.cpp")

# Specify the file to exclude
list(REMOVE_ITEM  OPENGLRENDERER_SOURCES "${PROJECT_SRC_DIR}/app/App.cpp")

file(GLOB_RECURSE OPENGLRENDERER_COMMIT_TEST_SUITE_SOURCES "*.cpp")

file(GLOB_RECURSE SHADER_HEADERS "${SHADER_SRC_DIR}/*.hpp")

file(GLOB_RECURSE OPENGLRENDERER_HEADERS "${SHADER_SRC_DIR}/*.hpp")

add_executable(${COMMIT_TEST_SUITE_OPENGL})

if(NOT MSVC)
  target_compile_definitions(${COMMIT_TEST_SUITE_OPENGL} PRIVATE RELATIVE_RESOURCE_PATH="/../../../../Resources/")
else()
  target_compile_definitions(${COMMIT_TEST_SUITE_OPENGL} PRIVATE RELATIVE_RESOURCE_PATH="/../../Resources/")
endif()
# we are using this dir strings for removing hard coded file locations
# at c++ side
configure_file(${PROJECT_SRC_DIR}/OpenGLRendererConfig.hpp.in "${PROJECT_SRC_DIR}/renderer/OpenGLRendererConfig.hpp")

target_sources(
  ${COMMIT_TEST_SUITE_OPENGL}
  PUBLIC ${OPENGLRENDERER_COMMIT_TEST_SUITE_SOURCES}
         ${PBR_SHADER_FILTER}
         ${BRDF_SHADER_FILTER}
         ${OPENGLRENDERER_SOURCES}
         ${COMMON_SHADER_FILTER}
         ${CLOUDS_SHADER_FILTER}
         ${LOADING_SHADER_FILTER}
         ${RASTERIZER_SHADER_FILTER}
         ${RASTERIZER_SHADOWS_SHADER_FILTER}
         ${SKYBOX_SHADER_FILTER}
         # this is great; no CPPCHECK,CLANG_TIDY here
         $<TARGET_OBJECTS:IMGUI>
         $<TARGET_OBJECTS:GLAD>
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${PROJECT_INCLUDE_DIR}
         ${SHADER_SRC_DIR}
         FILES
         ${OPENGLRENDERER_HEADERS}
         ${SHADER_HEADERS})

target_include_directories(${COMMIT_TEST_SUITE_OPENGL} PRIVATE ${OPENGL_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})

target_link_libraries(
  ${COMMIT_TEST_SUITE_OPENGL}
  PUBLIC gtest_main
         ${CMAKE_DL_LIBS} # for imgui
         Threads::Threads
         ${OPENGL_LIBRARIES}
         Vulkan::Vulkan
         glfw
         imgui
         stb
         glm
         tinyobjloader
         glad)

target_link_libraries(${COMMIT_TEST_SUITE_OPENGL} PRIVATE GSL spdlog)

gtest_discover_tests(${COMMIT_TEST_SUITE_OPENGL} DISCOVERY_TIMEOUT 300)

# disable all warnings for our test suite
if(MSVC)
  target_compile_options(${COMMIT_TEST_SUITE_OPENGL} INTERFACE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(${COMMIT_TEST_SUITE_OPENGL} INTERFACE -w)
else()
  target_compile_options(${COMMIT_TEST_SUITE_OPENGL} INTERFACE -w)
endif()
