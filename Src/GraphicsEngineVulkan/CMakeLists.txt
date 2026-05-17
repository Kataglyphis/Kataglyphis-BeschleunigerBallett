# update current positions
set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/Shaders/)

set(_KATAGLYPHIS_SOURCE_INCLUDE_DIRS
    ${SHADER_SRC_DIR}
    ${SHADER_SRC_DIR}common/
    ${SHADER_SRC_DIR}pbr/
    ${SHADER_SRC_DIR}pbr/brdf/
    ${SHADER_SRC_DIR}hostDevice/
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/pushConstants/
    ${CMAKE_CURRENT_SOURCE_DIR}/scene/)

include(${CMAKE_SOURCE_DIR}/cmake/KataglyphisCMakeHelpers.cmake)
include(cmake/filters/SetShaderFilters.cmake)
include(cmake/filters/SetProjectFilters.cmake)
include(cmake/SetSourceGroups.cmake)

file(GLOB_RECURSE VULKANRENDERER_SOURCES "*.cpp")

# Separate Main.cpp from library sources
set(VULKANRENDERER_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp")
list(REMOVE_ITEM VULKANRENDERER_SOURCES "${VULKANRENDERER_MAIN_SOURCE}")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/renderer/VulkanRenderer.cpp PROPERTIES COMPILE_OPTIONS
                                                                                                 "-fno-lto")
endif()

file(GLOB_RECURSE SHADER_HEADERS "${SHADER_SRC_DIR}*.hpp")

file(GLOB_RECURSE VULKANRENDERER_HEADERS "*.hpp")

kataglyphis_collect_module_interfaces(VULKAN_MODULE_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
kataglyphis_collect_module_interfaces(SHARED_MODULE_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

set(ShaderIncludes)
foreach(_kataglyphis_include_dir IN LISTS _KATAGLYPHIS_SOURCE_INCLUDE_DIRS)
  list(APPEND ShaderIncludes -I ${_kataglyphis_include_dir})
endforeach()

kataglyphis_join_list(ShaderIncludesString " " ${ShaderIncludes})

message("The shader include string is the following: ${ShaderIncludesString}")

# Generate config module file (shared between library and executable)
set(VULKAN_RENDERER_CONFIG_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(VULKAN_RENDERER_CONFIG_MODULE_FILE "${VULKAN_RENDERER_CONFIG_MODULE_DIR}/VulkanRendererConfig.ixx")
file(MAKE_DIRECTORY "${VULKAN_RENDERER_CONFIG_MODULE_DIR}")
configure_file(VulkanRendererConfig.ixx.in "${VULKAN_RENDERER_CONFIG_MODULE_FILE}" @ONLY)
set_source_files_properties("${VULKAN_RENDERER_CONFIG_MODULE_FILE}" PROPERTIES GENERATED TRUE)
list(APPEND VULKAN_MODULE_INTERFACE_FILES "${VULKAN_RENDERER_CONFIG_MODULE_FILE}")

# =============================================================================
# VulkanEngineCore - STATIC Library (compiled ONCE, reused by exe and tests)
# STATIC library handles C++20 module propagation better than OBJECT
# =============================================================================
add_library(VulkanEngineCore STATIC)

kataglyphis_set_module_scan(VulkanEngineCore)

if(RUST_FEATURES)
  target_compile_definitions(VulkanEngineCore PUBLIC USE_RUST=1)
else()
  target_compile_definitions(VulkanEngineCore PUBLIC USE_RUST=0)
endif()

kataglyphis_apply_runtime_compile_definitions(
  VulkanEngineCore
  RESOURCE_PATH_NON_MSVC
  "/../Resources/"
  RESOURCE_PATH_MSVC
  "/../../Resources/"
  INCLUDE_PATH_NON_MSVC
  "/../Src/GraphicsEngineVulkan/"
  INCLUDE_PATH_MSVC
  "/../../Src/GraphicsEngineVulkan/"
  IMGUI_FONTS_PATH_NON_MSVC
  "/../ExternalLib/IMGUI/misc/fonts/"
  IMGUI_FONTS_PATH_MSVC
  "/../../ExternalLib/IMGUI/misc/fonts/"
  SHADER_INCLUDES_STRING
  "${ShaderIncludesString}")

target_sources(
  VulkanEngineCore
  PRIVATE ${RASTER_SHADER_FILTER}
          ${RAYTRACING_SHADER_FILTER}
          ${COMMON_SHADER_FILTER}
          ${POST_SHADER_FILTER}
          ${BRDF_SHADER_FILTER}
          ${PBR_SHADER_FILTER}
          ${PATH_TRACING_SHADER_FILTER}
          ${COMPUTE_SHADER_FILTER}
          ${VULKANRENDERER_SOURCES}
  PUBLIC FILE_SET
         HEADERS
         BASE_DIRS
         ${PROJECT_INCLUDE_DIR}
         ${SHADER_SRC_DIR}
         FILES
         ${VULKANRENDERER_HEADERS}
         ${SHADER_HEADERS})

target_sources(
  VulkanEngineCore
  PUBLIC FILE_SET
         CXX_MODULES
         TYPE
         CXX_MODULES
         BASE_DIRS
         ${CMAKE_CURRENT_SOURCE_DIR}/..
         ${VULKAN_RENDERER_CONFIG_MODULE_DIR}
         FILES
         ${VULKAN_MODULE_INTERFACE_FILES}
         ${SHARED_MODULE_INTERFACE_FILES})

target_include_directories(VulkanEngineCore PUBLIC ${Vulkan_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(VulkanEngineCore PUBLIC VULKAN_HPP_NO_EXCEPTIONS VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

target_link_libraries(
  VulkanEngineCore
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
         # spdlog/nlohmann_json/GSL need to be PUBLIC because Main.cpp and test files use them
         GSL
         spdlog::spdlog
         nlohmann_json::nlohmann_json
         tomlplusplus_modules)

if(RUST_FEATURES)
  # Ensure CXX bridge headers are generated before module scanning
  add_dependencies(VulkanEngineCore kataglyphis_rustprojecttemplate_bridge)
  target_link_libraries(VulkanEngineCore PUBLIC kataglyphis_rustprojecttemplate kataglyphis_rustprojecttemplate_bridge)
endif()

# Enable ASan and UBSan in Debug mode on Linux (applied to library so tests also benefit)
target_compile_options(VulkanEngineCore PUBLIC $<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>>:-fsanitize=address>
                                               $<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>>:-fno-omit-frame-pointer>)
target_link_options(VulkanEngineCore PUBLIC $<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux>>:-fsanitize=address>)

# =============================================================================
# GraphicsEngine - Main Executable (uses VulkanEngineCore static library)
# =============================================================================
add_executable(${PROJECT_NAME})

kataglyphis_set_module_scan(${PROJECT_NAME})

target_sources(
  ${PROJECT_NAME}
  PRIVATE ${VULKANRENDERER_MAIN_SOURCE}
          # IMGUI object library - excluded from static analysis
          $<TARGET_OBJECTS:IMGUI>)

# Link VulkanEngineCore - modules with PUBLIC FILE_SET CXX_MODULES should propagate to consumers
# In CMake 3.28+, linking against a library with PUBLIC CXX_MODULES should make those modules importable
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanEngineCore absl::flags_parse)

# compile shaders as part of my cmake routine right away :)
include(cmake/CompileShadersToSPV.cmake)

set(ALL_VULKAN_SHADER_FILTERS
    ${POST_SHADER_FILTER}
    ${RAYTRACING_SHADER_FILTER}
    ${RASTER_SHADER_FILTER}
    ${PATH_TRACING_SHADER_FILTER}
    ${COMPUTE_SHADER_FILTER})

foreach(Shader ${ALL_VULKAN_SHADER_FILTERS})
  add_shader(${PROJECT_NAME} ${Shader})
endforeach()
