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

file(GLOB_RECURSE VULKANRENDERER_SOURCES "*.cpp")

file(
  GLOB_RECURSE SHADER_HEADERS
  RELATIVE ${SHADER_SRC_DIR}
  "*.hpp")

file(GLOB_RECURSE VULKANRENDERER_HEADERS "*.hpp")

kataglyphis_collect_module_interfaces(VULKAN_MODULE_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
kataglyphis_collect_module_interfaces(SHARED_MODULE_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

if(RUST_FEATURES)

  corrosion_import_crate(MANIFEST_PATH rusty_code/Cargo.toml)
  if(WIN32)
    corrosion_set_env_vars(rusty_code CXXFLAGS=/EHsc)
  else()
    # Native-only corrosion/toolchain setup (no cross-building)
    # Optionally override with -DGCC_TOOLCHAIN_PATH=/path/to/gcc when configuring
    set(_DEFAULT_GCC_TOOLCHAIN "")
    if(DEFINED MYPROJECT_GCC_TOOLCHAIN_PATH AND MYPROJECT_GCC_TOOLCHAIN_PATH)
      set(_DEFAULT_GCC_TOOLCHAIN "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
    endif()
    set(_HAS_DEFAULT_GCC_TOOLCHAIN OFF)
    if(EXISTS "${_DEFAULT_GCC_TOOLCHAIN}")
      set(_HAS_DEFAULT_GCC_TOOLCHAIN ON)
    endif()

    # Nur setzen, wenn wir im GNU/GCC Preset sind
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      if(_HAS_DEFAULT_GCC_TOOLCHAIN)
        set(GCC_TOOLCHAIN_PATH
            "${_DEFAULT_GCC_TOOLCHAIN}"
            CACHE PATH "GCC toolchain root")
        set(RUST_CXXFLAGS "--gcc-toolchain=${_DEFAULT_GCC_TOOLCHAIN} -fexceptions")
      else()
        set(GCC_TOOLCHAIN_PATH
            ""
            CACHE PATH "GCC toolchain root" FORCE)
        set(RUST_CXXFLAGS "-fexceptions")
      endif()
    else()
      # Falls Clang aktiv ist, nur dann toolchain-Flag setzen, wenn der Pfad existiert
      set(GCC_TOOLCHAIN_PATH
          ""
          CACHE PATH "GCC toolchain root" FORCE)
      if(_HAS_DEFAULT_GCC_TOOLCHAIN)
        set(RUST_CXXFLAGS "--gcc-toolchain=${_DEFAULT_GCC_TOOLCHAIN} -fexceptions")
      else()
        set(RUST_CXXFLAGS "-fexceptions")
      endif()
    endif()

    # detect host arch for logging (no cross-compile behavior)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _hostproc)
    if(_hostproc MATCHES "^(x86_64|amd64)$")
      set(HOST_ARCH "x86_64")
    elseif(_hostproc MATCHES "^(aarch64|arm64|armv8)$")
      set(HOST_ARCH "aarch64")
    elseif(_hostproc MATCHES "^(riscv64)$")
      set(HOST_ARCH "riscv64")
    else()
      set(HOST_ARCH "${_hostproc}")
    endif()

    message(STATUS "Native-only build. HOST_ARCH=${HOST_ARCH}")

    if(GCC_TOOLCHAIN_PATH AND EXISTS "${GCC_TOOLCHAIN_PATH}")
      # Try to find reasonable native compiler binary names
      if(HOST_ARCH STREQUAL "x86_64")
        find_program(GCC_TOOLCHAIN_GCC NAMES "${GCC_TOOLCHAIN_PATH}/bin/x86_64-pc-linux-gnu-gcc"
                                             "${GCC_TOOLCHAIN_PATH}/bin/gcc")
        find_program(GCC_TOOLCHAIN_GXX NAMES "${GCC_TOOLCHAIN_PATH}/bin/x86_64-pc-linux-gnu-g++"
                                             "${GCC_TOOLCHAIN_PATH}/bin/g++")
      elseif(HOST_ARCH STREQUAL "aarch64")
        find_program(GCC_TOOLCHAIN_GCC NAMES "${GCC_TOOLCHAIN_PATH}/bin/aarch64-linux-gnu-gcc"
                                             "${GCC_TOOLCHAIN_PATH}/bin/gcc")
        find_program(GCC_TOOLCHAIN_GXX NAMES "${GCC_TOOLCHAIN_PATH}/bin/aarch64-linux-gnu-g++"
                                             "${GCC_TOOLCHAIN_PATH}/bin/g++")
      elseif(HOST_ARCH STREQUAL "riscv64")
        find_program(GCC_TOOLCHAIN_GCC NAMES "${GCC_TOOLCHAIN_PATH}/bin/riscv64-unknown-linux-gnu-gcc"
                                             "${GCC_TOOLCHAIN_PATH}/bin/gcc")
        find_program(GCC_TOOLCHAIN_GXX NAMES "${GCC_TOOLCHAIN_PATH}/bin/riscv64-unknown-linux-gnu-g++"
                                             "${GCC_TOOLCHAIN_PATH}/bin/g++")
      else()
        find_program(GCC_TOOLCHAIN_GCC NAMES "${GCC_TOOLCHAIN_PATH}/bin/gcc")
        find_program(GCC_TOOLCHAIN_GXX NAMES "${GCC_TOOLCHAIN_PATH}/bin/g++")
      endif()

      if(NOT GCC_TOOLCHAIN_GCC)
        message(WARNING "Toolchain gcc not found in ${GCC_TOOLCHAIN_PATH}; falling back to system 'gcc'")
        set(GCC_TOOLCHAIN_GCC "gcc")
      endif()
      if(NOT GCC_TOOLCHAIN_GXX)
        message(WARNING "Toolchain g++ not found in ${GCC_TOOLCHAIN_PATH}; falling back to system 'g++'")
        set(GCC_TOOLCHAIN_GXX "g++")
      endif()

      # prefer lib64 then lib; optional for setting LDFLAGS/rpath
      if(EXISTS "${GCC_TOOLCHAIN_PATH}/lib64")
        set(GCC_LIB_PATH "${GCC_TOOLCHAIN_PATH}/lib64")
      elseif(EXISTS "${GCC_TOOLCHAIN_PATH}/lib")
        set(GCC_LIB_PATH "${GCC_TOOLCHAIN_PATH}/lib")
      else()
        message(WARNING "No lib or lib64 under ${GCC_TOOLCHAIN_PATH}; skipping LDFLAGS/rpath setup")
        unset(GCC_LIB_PATH)
      endif()

      # DO NOT inject toolchain C++ includes. Only set CC/CXX and LDFLAGS/rpath for native run.
      corrosion_set_env_vars(
        rusty_code
        CC=${GCC_TOOLCHAIN_GCC}
        CXX=${GCC_TOOLCHAIN_GXX}
        "CXXFLAGS=${RUST_CXXFLAGS}")

      message(
        STATUS "Configured native corrosion: CC=${GCC_TOOLCHAIN_GCC} CXX=${GCC_TOOLCHAIN_GXX} CXXFLAGS=${RUST_CXXFLAGS}"
      )
      if(GCC_LIB_PATH)
        message(STATUS "GCC library path: ${GCC_LIB_PATH}")
      endif()
    else()
      message(STATUS "No GCC_TOOLCHAIN_PATH (${GCC_TOOLCHAIN_PATH}) found — using system compilers (native build)")
      # Still set minimal sensible env so corrosion has deterministic env on non-Windows hosts
      corrosion_set_env_vars(rusty_code "CXXFLAGS=${RUST_CXXFLAGS}")
    endif()
  endif()

  corrosion_add_cxxbridge(
    rusty_bridge
    CRATE
    rusty_code
    FILES
    lib.rs)
endif()

# add the executable
add_executable(${PROJECT_NAME})

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

if(RUST_FEATURES)
  target_compile_definitions(${PROJECT_NAME} PRIVATE USE_RUST=1)
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE USE_RUST=0)
endif()

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

configure_file(VulkanRendererConfig.hpp.in "${CMAKE_CURRENT_SOURCE_DIR}/renderer/VulkanRendererConfig.hpp")

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

target_sources(
  ${PROJECT_NAME}
  PUBLIC FILE_SET
         CXX_MODULES
         TYPE
         CXX_MODULES
         BASE_DIRS
         ${CMAKE_CURRENT_SOURCE_DIR}/..
         FILES
         ${VULKAN_MODULE_INTERFACE_FILES}
         ${SHARED_MODULE_INTERFACE_FILES})

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
  target_link_libraries(${PROJECT_NAME} PUBLIC rusty_code)
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
