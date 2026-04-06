cmake_minimum_required(VERSION 4.2.0)

# set the project name
file(READ "version.txt" PROJECT_VERSION_STR)
string(STRIP "${PROJECT_VERSION_STR}" PROJECT_VERSION_STR)

project(
  GraphicsEngine
  VERSION ${PROJECT_VERSION_STR}
  DESCRIPTION "Epic GPU accelerated rendering, compute engine using Vulkan and C++,C,Rust"
  HOMEPAGE_URL "https://jonasheinle.de/"
  LANGUAGES NONE)

# Make version available to C++ code globally
add_compile_definitions(PROJECT_VERSION="${PROJECT_VERSION_STR}")

set(AUTHOR "Jonas Heinle")

# Set WINDOWS_CI to OFF by default
# WINDOWS CI is only able to verify successful compiling ... Windows ... lel
# i will stay on linux ...
set(WINDOWS_CI
    OFF
    CACHE BOOL "Enable Windows CI build options")
set(RUST_FEATURES
    ON
    CACHE BOOL "Enable Rust features in our project.")
set(MYPROJECT_CMAKE_DEBUG_LOG
    OFF
    CACHE BOOL "Enable additional verbose CMake configure logging")

set(MYPROJECT_GCC_TOOLCHAIN_PATH
    ""
    CACHE PATH "Optional GCC toolchain root used by Clang and Rust tooling on Linux (for example /opt/gcc-15.2.0)")

# Auto-detect custom GCC toolchain for Clang on Linux to avoid workflow env setup
if(UNIX
   AND NOT APPLE
   AND MYPROJECT_GCC_TOOLCHAIN_PATH)
  set(_GCC_TOOLCHAIN_ROOT "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
  set(_GCC_TOOLCHAIN_LIB "${_GCC_TOOLCHAIN_ROOT}/lib64")
  if(NOT EXISTS "${_GCC_TOOLCHAIN_LIB}")
    set(_GCC_TOOLCHAIN_LIB "${_GCC_TOOLCHAIN_ROOT}/lib")
  endif()

  if(EXISTS "${_GCC_TOOLCHAIN_LIB}")
    message(STATUS "Using GCC toolchain at ${_GCC_TOOLCHAIN_ROOT} for Clang")
    string(APPEND CMAKE_C_FLAGS " --gcc-toolchain=${_GCC_TOOLCHAIN_ROOT}")
    string(APPEND CMAKE_CXX_FLAGS " --gcc-toolchain=${_GCC_TOOLCHAIN_ROOT}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS
           " -L${_GCC_TOOLCHAIN_LIB} -Wl,-rpath,${_GCC_TOOLCHAIN_LIB} --gcc-toolchain=${_GCC_TOOLCHAIN_ROOT}")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS
           " -L${_GCC_TOOLCHAIN_LIB} -Wl,-rpath,${_GCC_TOOLCHAIN_LIB} --gcc-toolchain=${_GCC_TOOLCHAIN_ROOT}")
  else()
    message(WARNING "GCC toolchain found at ${_GCC_TOOLCHAIN_ROOT}, but no lib or lib64 dir exists.")
  endif()
endif()

enable_language(CXX C)

if(RUST_FEATURES)
  message(STATUS "Rust features are enabled.")
  if(MSVC)
    message(STATUS "On a windows machine you must use MSVC when integrating rust features.")
  endif()
else()
  message(STATUS "Rust features are disabled.")
  message(STATUS "Your are going to use a non MSVC compiler on windows. Therefore no rust integration possible.")
endif()

include(cmake/PreventInSourceBuilds.cmake)
include(cmake/ProjectOptions.cmake)

myproject_setup_options()

myproject_global_options()

myproject_local_options()

if(myproject_ENABLE_COVERAGE
   AND NOT
       CMAKE_BUILD_TYPE
       STREQUAL
       "Release"
   AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()

include(cmake/SystemLibDependencies.cmake)
include(Src/GraphicsEngineVulkan/cmake/filters/SetExternalLibsFilters.cmake)
add_subdirectory(ExternalLib)

if(BUILD_TESTING
   AND NOT WIN32
   AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   AND NOT (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
  # Fuzz flags should be applied on the target, not globally
endif()

add_subdirectory(Src)

if(BUILD_TESTING)
  include(CTest)
  enable_testing()
  message(STATUS "BUILD_TESTING is ON - enabling CTest.")
else()
  message(STATUS "BUILD_TESTING is OFF - skipping CTest enablement.")
endif()

if(BUILD_TESTING)
  message(STATUS "Tests are enabled.!")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_subdirectory(Test/commit)
    add_subdirectory(Test/compile)
    add_subdirectory(Test/fuzz)
    message(STATUS "Building test suites: commit, compile, fuzz (Debug)")
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_subdirectory(Test/perf)
    message(STATUS "Building test suite: perf (RelWithDebInfo)")
  else()
    message(
      STATUS
        "Skipping test suites for CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'. Use Debug for commit/compile/fuzz or RelWithDebInfo for perf."
    )
  endif()
else()
  message(STATUS "Tests are disabled.!")
endif()

# for correct library output needed
install(
  TARGETS ${PROJECT_NAME}
  CONFIGURATIONS Release
  RUNTIME DESTINATION bin)

# we keep the relative paths in the install dir
# keep in mind that install dir will be in front of this relative path ...
# I want to use the .ico file in the images folder
install(DIRECTORY images/ DESTINATION images/)
install(DIRECTORY Resources/Shaders/ DESTINATION Resources/Shaders/)
install(DIRECTORY Resources/Textures/ DESTINATION Resources/Textures/)
install(DIRECTORY Resources/Models/crytek-sponza/ DESTINATION Resources/Models/crytek-sponza/)
install(
  FILES ExternalLib/IMGUI/misc/fonts/Cousine-Regular.ttf
        ExternalLib/IMGUI/misc/fonts/DroidSans.ttf
        ExternalLib/IMGUI/misc/fonts/Karla-Regular.ttf
        ExternalLib/IMGUI/misc/fonts/ProggyClean.ttf
        ExternalLib/IMGUI/misc/fonts/ProggyTiny.ttf
        ExternalLib/IMGUI/misc/fonts/Roboto-Medium.ttf
  DESTINATION ExternalLib/IMGUI/misc/fonts/)
if(UNIX AND NOT APPLE)
  install(FILES docs/packaging/GraphicsEngine.desktop DESTINATION usr/share/applications)
  install(
    FILES docs/packaging/GraphicsEngine.appdata.xml
    DESTINATION usr/share/metainfo
    RENAME de.jonasheinle.GraphicsEngine.appdata.xml)
  install(
    FILES images/Engine_logo.png
    DESTINATION usr/share/icons/hicolor/256x256/apps
    RENAME Engine_logo.png)
  install(
    FILES images/Engine_logo.png
    DESTINATION usr/share/icons/hicolor/256x256/apps
    RENAME Engine_logo)
  install(
    FILES images/Engine_logo.png
    DESTINATION .
    RENAME Engine_logo.png)
endif()

include(cmake/CPackOptions.cmake)
