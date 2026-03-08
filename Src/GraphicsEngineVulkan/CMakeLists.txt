include(${CMAKE_SOURCE_DIR}/cmake/EngineBackendMacros.cmake)
kataglyphis_setup_engine_dirs_and_filters()

file(GLOB_RECURSE VULKANRENDERER_SOURCES "*.cpp")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/renderer/VulkanRenderer.cpp PROPERTIES COMPILE_OPTIONS
                                                                                                 "-fno-lto")
endif()

file(
  GLOB_RECURSE SHADER_HEADERS
  RELATIVE ${SHADER_SRC_DIR}
  "*.hpp")

file(GLOB_RECURSE VULKANRENDERER_HEADERS "*.hpp")

kataglyphis_collect_module_interfaces(VULKAN_MODULE_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
kataglyphis_collect_module_interfaces(SHARED_MODULE_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

# add the executable
add_executable(${PROJECT_NAME})

kataglyphis_setup_engine_backend(${PROJECT_NAME} Vulkan VULKAN)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/
    -I
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/
    -I
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/pushConstants/
    -I
    ${CMAKE_CURRENT_SOURCE_DIR}/scene/)

# Join the paths into a single space-separated string
string(
  REPLACE ";"
          " "
          ShaderIncludesString
          "${ShaderIncludes}")

message("The shader include string is the following: ${ShaderIncludesString}")

kataglyphis_apply_runtime_compile_definitions(
  ${PROJECT_NAME}
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
  ${PROJECT_NAME}
  PUBLIC ${RASTER_SHADER_FILTER}
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

target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

target_link_libraries(
  ${PROJECT_NAME}
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
  PRIVATE GSL spdlog::spdlog nlohmann_json::nlohmann_json)

if(RUST_FEATURES)
  target_link_libraries(${PROJECT_NAME} PUBLIC kataglyphis_rustprojecttemplate kataglyphis_rustprojecttemplate_bridge)
endif()

# compile shaders as part of my cmake routine right away :)
include(cmake/CompileShadersToSPV.cmake)

set(ALL_VULKAN_SHADER_FILTERS
    ${POST_SHADER_FILTER}
    ${RAYTRACING_SHADER_FILTER}
    ${RASTER_SHADER_FILTER}
    ${PATH_TRACING_SHADER_FILTER})

foreach(Shader ${ALL_VULKAN_SHADER_FILTERS})
  add_shader(${PROJECT_NAME} ${Shader})
endforeach()
