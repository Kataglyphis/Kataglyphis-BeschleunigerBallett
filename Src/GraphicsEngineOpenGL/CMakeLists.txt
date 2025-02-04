# update current positions
set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(EXTERNAL_LIB_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}../../ExternalLib/)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/Shaders/)

include(cmake/filters/SetShaderFilters.cmake)
include(cmake/filters/SetProjectFilters.cmake)
include(cmake/SetSourceGroups.cmake)
include(cmake/filters/SetExternalLibsFilters.cmake)

file(GLOB_RECURSE OPENGLRENDERER_SOURCES "*.cpp")

file(GLOB_RECURSE SHADER_HEADERS "${SHADER_SRC_DIR}/*.hpp")

file(GLOB_RECURSE OPENGLRENDERER_HEADERS "*.hpp")

set(OpenGlEnigneTargetName "GraphicsEngineOpenGLBackend")

# add the executable
add_executable(${OpenGlEnigneTargetName})

if(NOT MSVC)
  target_compile_definitions(${OpenGlEnigneTargetName} PRIVATE RELATIVE_RESOURCE_PATH="/../Resources/")
else()
  target_compile_definitions(${OpenGlEnigneTargetName} PRIVATE RELATIVE_RESOURCE_PATH="/../../Resources/")
endif()

# we are using this dir strings for removing hard coded file locations
# at c++ side
configure_file(OpenGLRendererConfig.hpp.in "renderer/OpenGLRendererConfig.hpp")

target_sources(
  ${OpenGlEnigneTargetName}
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

target_include_directories(${OpenGlEnigneTargetName} PRIVATE ${OPENGL_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})

target_link_libraries(
  ${OpenGlEnigneTargetName}
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
