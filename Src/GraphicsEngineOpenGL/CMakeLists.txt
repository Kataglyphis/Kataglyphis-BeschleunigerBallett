# update current positions
set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(EXTERNAL_LIB_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}../../ExternalLib/)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/Shaders/)

include(cmake/filters/SetShaderFilters.cmake)
include(cmake/filters/SetProjectFilters.cmake)
include(cmake/SetSourceGroups.cmake)
include(cmake/filters/SetExternalLibsFilters.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)

file(GLOB_RECURSE OPENGLRENDERER_SOURCES "*.cpp")

file(GLOB_RECURSE SHADER_HEADERS "${SHADER_SRC_DIR}/*.hpp")

file(GLOB_RECURSE OPENGLRENDERER_HEADERS "*.hpp")

kataglyphis_collect_module_interfaces(OPENGL_MODULE_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")

set(OpenGlEngineTargetName "GraphicsEngineOpenGLBackend")

# add the executable
add_executable(${OpenGlEngineTargetName})

set_target_properties(${OpenGlEngineTargetName} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

kataglyphis_apply_runtime_compile_definitions(
  ${OpenGlEngineTargetName}
  RESOURCE_PATH_NON_MSVC
  "/../Resources/"
  RESOURCE_PATH_MSVC
  "/../../Resources/")

# we are using this dir strings for removing hard coded file locations
# at c++ side
configure_file(OpenGLRendererConfig.hpp.in "renderer/OpenGLRendererConfig.hpp")

target_sources(
  ${OpenGlEngineTargetName}
  PUBLIC ${OPENGLRENDERER_SOURCES}
         ${PBR_SHADER_FILTER}
         ${BRDF_SHADER_FILTER}
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

target_sources(
  ${OpenGlEngineTargetName}
  PUBLIC FILE_SET
         CXX_MODULES
         TYPE
         CXX_MODULES
         FILES
         ${OPENGL_MODULE_INTERFACE_FILES})

target_include_directories(${OpenGlEngineTargetName} PRIVATE ${OPENGL_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})

target_link_libraries(
  ${OpenGlEngineTargetName}
  PUBLIC # for imgui
         ${CMAKE_DL_LIBS}
         Threads::Threads
         ${OPENGL_LIBRARIES}
         Vulkan::Vulkan
         glfw
         imgui
         stb
         glm
         tinyobjloader
         glad
         # enable compiler warnings
         myproject_warnings
         # enable sanitizers
         myproject_options
  PRIVATE GSL spdlog nlohmann_json::nlohmann_json)
