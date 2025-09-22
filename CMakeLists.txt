cmake_minimum_required(VERSION 4.1.1)

# set the project name
project(
  GraphicsEngine
  VERSION 1.3
  DESCRIPTION "Epic Graphics"
  HOMEPAGE_URL "https://jonasheinle.de/"
  LANGUAGES CXX C)

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

include(cmake/SystemLibDependencies.cmake)
include(Src/GraphicsEngineVulkan/cmake/filters/SetExternalLibsFilters.cmake)
add_subdirectory(ExternalLib)

add_subdirectory(Src)

if(NOT WINDOWS_CI AND BUILD_TESTING)
  include(CTest)
  enable_testing()
  message(STATUS "WINDOWS_CI is OFF or not defined.")
else()
  message(STATUS "WINDOWS_CI is OFF or not defined.")
endif()

if(BUILD_TESTING)
  message(STATUS "Tests are enabled.!")
  add_subdirectory(Test/commit)
  add_subdirectory(Test/compile)
  add_subdirectory(Test/perf)
  add_subdirectory(Test/fuzz)
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
install(DIRECTORY ExternalLib/IMGUI/misc/fonts/ DESTINATION ExternalLib/IMGUI/misc/fonts/)

include(cmake/CPackOptions.cmake)
