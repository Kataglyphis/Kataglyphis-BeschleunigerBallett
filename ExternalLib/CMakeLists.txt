include(FetchContent)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(BUILD_TESTING)
  # include google test to project
  FetchContent_Declare(googletest
                       URL https://github.com/google/googletest/archive/e235eb34c6c4fed790ccdad4b16394301360dcd4.zip)
  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# We will not need to test benchmark lib itself.
set(BENCHMARK_ENABLE_TESTING
    OFF
    CACHE BOOL "Disable benchmark testing as we don't need it.")
# We will not need to test benchmark lib itself.
set(BENCHMARK_INSTALL_DOCS
    OFF
    CACHE BOOL "Disable benchmark testing as we don't need it.")
# We will not need to install benchmark since we link it statically.
set(BENCHMARK_ENABLE_INSTALL
    OFF
    CACHE BOOL "Disable benchmark install to avoid overwriting vendor install.")
set(BENCHMARK_USE_BUNDLED_GTEST
    OFF
    CACHE BOOL "Disable the bundled GTest usage.")

add_subdirectory(GOOGLE_BENCHMARK)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND BUILD_TESTING AND NOT(CMAKE_BUILD_TYPE STREQUAL "Profile" ))
  message(STATUS "This is a Linux system.")

  if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")

    message("Adding Fuzztest")
    add_subdirectory(FUZZTEST)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
      set(FUZZTEST_FUZZING_MODE
          ON
          CACHE BOOL "" FORCE)
    endif()
  endif()
endif()

FetchContent_Declare(
  GSL
  GIT_REPOSITORY "https://github.com/microsoft/GSL"
  GIT_TAG "v4.1.0")
FetchContent_MakeAvailable(GSL)

# set all glfw dependencies
set(GLFW_BUILD_DOCS
    OFF
    CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS
    OFF
    CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES
    OFF
    CACHE BOOL "" FORCE)

add_library(tinyobjloader INTERFACE)
# this helps us so no error messages for third party will be generated:
# https://stackoverflow.com/questions/49591804/clang-tidy-cmake-exclude-file-from-check
target_include_directories(tinyobjloader SYSTEM INTERFACE TINY_OBJ_LOADER)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE STB)

add_library(imgui INTERFACE)
target_include_directories(imgui SYSTEM INTERFACE IMGUI IMGUI/backends)

add_library(glm INTERFACE)
target_include_directories(glm SYSTEM INTERFACE GLM)

add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE VULKAN_MEMORY_ALLOCATOR/include)

add_library(ktx INTERFACE)
target_include_directories(ktx SYSTEM INTERFACE KTX/include KTX/other_include/KHR)

add_subdirectory(GLFW)

if(myproject_DISABLE_EXCEPTIONS)
  set(SPDLOG_NO_EXCEPTIONS
      ON
      CACHE BOOL "Disable SPDLOG exceptions.")
endif()

add_subdirectory(SPDLOG)

add_subdirectory(NLOHMANN_JSON)

add_library(glad INTERFACE)
target_include_directories(glad SYSTEM INTERFACE GLAD/include)

# https://stackoverflow.com/questions/49591804/clang-tidy-cmake-exclude-file-from-check/49591908#49591908
# with this trick IMGUI will be excluded from time consuming static code analysis
# we do not want to analyze imgui ...
# and we will not to all this for our testing executable again :)
add_library(IMGUI OBJECT ${IMGUI_FILTER})

target_link_libraries(
  IMGUI
  PRIVATE ${CMAKE_DL_LIBS}
          Vulkan::Vulkan
          ${OPENGL_LIBRARIES}
          glfw
          imgui)

target_include_directories(IMGUI PRIVATE ${Vulkan_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIRS})

set_target_properties(IMGUI PROPERTIES CXX_CLANG_TIDY "" CXX_CPPCHECK "")

# GLAD LIB
add_library(GLAD OBJECT ${GLAD_FILTER})

target_link_libraries(GLAD PRIVATE ${OPENGL_LIBRARIES})

target_include_directories(GLAD PRIVATE GLAD/include ${OPENGL_INCLUDE_DIRS})

set_target_properties(GLAD PROPERTIES CXX_CLANG_TIDY "" CXX_CPPCHECK "")

if(RUST_FEATURES)
  FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG master)
  FetchContent_MakeAvailable(Corrosion)

  find_program(CXXBRIDGE cxxbridge PATHS "$ENV{HOME}/.cargo/bin/")
  if(CXXBRIDGE STREQUAL "CXXBRIDGE-NOTFOUND")
    message("Could not find cxxbridge, trying to install with `cargo install cxxbridge-cmd'")
    find_program(CARGO cargo PATHS "$ENV{HOME}/.cargo/bin/")
    if(CARGO STREQUAL "CARGO-NOTFOUND")
      message(FATAL_ERROR "Requires cargo available in path, install via rustup https://rustup.rs/")
    endif()
    execute_process(COMMAND ${CARGO} install cxxbridge-cmd)
    find_program(CXXBRIDGE cxxbridge PATHS "$ENV{HOME}/.cargo/bin/")
  endif()
endif()

set(KOMPUTE_OPT_BUILD_EXAMPLES OFF)
set(KOMPUTE_OPT_BUILD_TESTS OFF)
set(KOMPUTE_OPT_USE_VULKAN_VERSION "1.3.296.0")
set(KOMPUTE_OPT_DISABLE_VULKAN_VERSION_CHECK ON)

if(WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()
add_subdirectory(KOMPUTE)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)
