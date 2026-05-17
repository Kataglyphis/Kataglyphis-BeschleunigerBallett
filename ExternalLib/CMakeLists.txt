include(FetchContent)

function(_myproject_collect_targets_recursive dir out_var)
  get_property(_dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  set(_targets ${_dir_targets})

  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    _myproject_collect_targets_recursive("${_subdir}" _subdir_targets)
    list(APPEND _targets ${_subdir_targets})
  endforeach()

  set(${out_var} "${_targets}" PARENT_SCOPE)
endfunction()

function(_myproject_append_link_library target dependency)
  if(NOT TARGET "${target}" OR NOT TARGET "${dependency}")
    return()
  endif()

  get_target_property(_target_type "${target}" TYPE)
  if(NOT _target_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
    return()
  endif()

  get_target_property(_existing_link_libraries "${target}" LINK_LIBRARIES)
  list(FIND _existing_link_libraries "${dependency}" _dependency_index)
  if(_dependency_index EQUAL -1)
    # Append directly to LINK_LIBRARIES so we can extend third-party targets
    # regardless of whether they used the plain or keyword TLL signature.
    set_property(TARGET "${target}" APPEND PROPERTY LINK_LIBRARIES "${dependency}")
  endif()
endfunction()

set(ABSL_TAG 20260107.1)
FetchContent_Declare(
  abseil-cpp
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG ${ABSL_TAG}
  GIT_SHALLOW 1
)
set(ABSL_PROPAGATE_CXX_STD ON)
set(BUILD_TESTING_OLD "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(abseil-cpp)
set(BUILD_TESTING "${BUILD_TESTING_OLD}")

if(WIN32 AND MSVC AND KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE)
  add_library(kataglyphis_fuzztest_windows_asan INTERFACE)
  myproject_enable_sanitizers(kataglyphis_fuzztest_windows_asan ON OFF OFF OFF OFF)

  # Keep Windows STL annotation mode consistent across the external fuzz build.
  add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION _DISABLE_OPTIONAL_ANNOTATION)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
   AND MSVC
   AND myproject_ENABLE_SANITIZER_ADDRESS)
  # Abseil overrides the MSVC runtime selection in its own CMakeLists.
  # Force it back to /MD so its STL mode matches the rest of the Windows ASan build.
  _myproject_collect_targets_recursive("${CMAKE_CURRENT_BINARY_DIR}" _external_targets)
  foreach(_external_target IN LISTS _external_targets)
    if(_external_target MATCHES "^absl_")
      get_target_property(_external_target_type "${_external_target}" TYPE)
      if(NOT _external_target_type STREQUAL "INTERFACE_LIBRARY")
        set_property(TARGET "${_external_target}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

        if(TARGET kataglyphis_fuzztest_windows_asan)
          target_link_libraries("${_external_target}" PRIVATE kataglyphis_fuzztest_windows_asan)
        endif()
      endif()
    endif()
  endforeach()
endif()

if(BUILD_TESTING)
  # include google test to project
  FetchContent_Declare(googletest
                       URL https://github.com/google/googletest/archive/56efe3983185e3f37e43415d1afa97e3860f187f.zip)

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

if(myproject_DISABLE_EXCEPTIONS)
  set(BENCHMARK_ENABLE_EXCEPTIONS
    OFF
    CACHE BOOL "Disable benchmark exceptions to match project configuration." FORCE)
else()
  set(BENCHMARK_ENABLE_EXCEPTIONS
    ON
    CACHE BOOL "Enable benchmark exceptions to match project configuration." FORCE)
endif()

add_subdirectory(GOOGLE_BENCHMARK)

if(BUILD_TESTING
   AND NOT (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
   AND NOT (CMAKE_BUILD_TYPE STREQUAL "Release"))

  if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang"
     AND (CMAKE_SYSTEM_NAME STREQUAL "Linux"
          OR (WIN32 AND MSVC AND KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE)))
    set(RE2_INSTALL OFF)
    set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "Disable vendored ANTLR runtime tests." FORCE)

    message("Adding Fuzztest")

    add_subdirectory(FUZZTEST)

    if(WIN32 AND MSVC AND KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE)
      if(TARGET kataglyphis_fuzztest_windows_asan)
        foreach(_fuzztest_windows_asan_target IN ITEMS
                antlr_frontend
                backend
                code_generation
                generated_antlr_parser
                grammar_domain_code_generator)
          _myproject_append_link_library("${_fuzztest_windows_asan_target}" kataglyphis_fuzztest_windows_asan)
        endforeach()

        _myproject_collect_targets_recursive("${CMAKE_CURRENT_BINARY_DIR}" _external_targets)
        foreach(_external_target IN LISTS _external_targets)
          if(_external_target MATCHES "^fuzztest_")
            _myproject_append_link_library("${_external_target}" kataglyphis_fuzztest_windows_asan)
          endif()
        endforeach()
      endif()

      if(TARGET re2 AND TARGET kataglyphis_fuzztest_windows_asan)
        target_link_libraries(re2 PRIVATE kataglyphis_fuzztest_windows_asan)
      endif()

      foreach(_antlr_target IN ITEMS antlr4_static antlr4_shared)
        if(TARGET "${_antlr_target}")
          # Keep ANTLR on the same CRT/STL mode as the clang-cl fuzz build.
          set_property(TARGET "${_antlr_target}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

          # ANTLR 4.13.2 misses <chrono> in ProfilingATNSimulator.cpp.
          target_compile_options("${_antlr_target}" PRIVATE /FIchrono)
        endif()
      endforeach()
    endif()
  endif()
endif()

FetchContent_Declare(
  GSL
  GIT_REPOSITORY "https://github.com/microsoft/GSL"
  GIT_TAG "v4.2.1")
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

set(SPDLOG_NO_EXCEPTIONS
    ON
    CACHE BOOL "Disable SPDLOG exceptions (required when exceptions are disabled globally)" FORCE)

add_subdirectory(SPDLOG)

add_subdirectory(NLOHMANN_JSON)

set(TOMLPLUSPLUS_BUILD_MODULES
    ON
    CACHE BOOL "Enable tomlplusplus C++ module support" FORCE)
add_subdirectory(tomlplusplus)

# https://stackoverflow.com/questions/49591804/clang-tidy-cmake-exclude-file-from-check/49591908#49591908
# with this trick IMGUI will be excluded from time consuming static code analysis
# we do not want to analyze imgui ...
# and we will not to all this for our testing executable again :)
add_library(IMGUI OBJECT ${IMGUI_FILTER})

target_link_libraries(
  IMGUI
  PRIVATE ${CMAKE_DL_LIBS}
          Vulkan::Vulkan
          glfw
          imgui)

target_include_directories(IMGUI PRIVATE ${Vulkan_INCLUDE_DIRS})

set_target_properties(IMGUI PROPERTIES CXX_CLANG_TIDY "" CXX_CPPCHECK "")

if(RUST_FEATURES)
  if(WIN32)
    set(Rust_CARGO
        "${CMAKE_SOURCE_DIR}/Scripts/Windows/cargo-retry.cmd"
        CACHE FILEPATH "Cargo wrapper used to retry transient Windows file-lock failures" FORCE)
  endif()

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

# Determine Vulkan version dynamically, falling back to the previous constant if nothing is detected.
set(KOMPUTE_OPT_USE_VULKAN_VERSION "" CACHE STRING "Override the Vulkan version Kompute requests.")
if(KOMPUTE_OPT_USE_VULKAN_VERSION STREQUAL "")
  if(DEFINED Vulkan_VERSION)
    set(KOMPUTE_OPT_USE_VULKAN_VERSION "${Vulkan_VERSION}")
  elseif(DEFINED VULKAN_VERSION_MAJOR AND DEFINED VULKAN_VERSION_MINOR AND DEFINED VULKAN_VERSION_PATCH)
    set(KOMPUTE_OPT_USE_VULKAN_VERSION "${VULKAN_VERSION_MAJOR}.${VULKAN_VERSION_MINOR}.${VULKAN_VERSION_PATCH}")
  elseif(DEFINED Vulkan_VERSION_MAJOR AND DEFINED Vulkan_VERSION_MINOR AND DEFINED Vulkan_VERSION_PATCH)
    set(KOMPUTE_OPT_USE_VULKAN_VERSION "${Vulkan_VERSION_MAJOR}.${Vulkan_VERSION_MINOR}.${Vulkan_VERSION_PATCH}")
  else()
    set(KOMPUTE_OPT_USE_VULKAN_VERSION "1.4.328.1")
  endif()
else()
  message(STATUS "Kompute Vulkan version overridden to ${KOMPUTE_OPT_USE_VULKAN_VERSION}")
endif()

set(KOMPUTE_OPT_BUILT_IN_VULKAN_HEADER_TAG_OVERRIDE ""
    CACHE STRING "Optional manual override for the built-in Vulkan-Headers tag Kompute uses (for example v1.4.328).")

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _kompute_vulkan_semver "${KOMPUTE_OPT_USE_VULKAN_VERSION}")
if(_kompute_vulkan_semver)
  set(_kompute_default_header_tag "v${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
else()
  set(_kompute_default_header_tag "v1.3.231")
endif()

if(KOMPUTE_OPT_BUILT_IN_VULKAN_HEADER_TAG_OVERRIDE STREQUAL "")
  set(KOMPUTE_OPT_BUILT_IN_VULKAN_HEADER_TAG "${_kompute_default_header_tag}" CACHE STRING "Resolved Vulkan-Headers tag Kompute uses." FORCE)
  message(STATUS "Kompute Vulkan-Headers tag set from detected Vulkan version: ${KOMPUTE_OPT_BUILT_IN_VULKAN_HEADER_TAG}")
else()
  set(KOMPUTE_OPT_BUILT_IN_VULKAN_HEADER_TAG "${KOMPUTE_OPT_BUILT_IN_VULKAN_HEADER_TAG_OVERRIDE}" CACHE STRING "Resolved Vulkan-Headers tag Kompute uses." FORCE)
  message(STATUS "Kompute Vulkan-Headers tag overridden to ${KOMPUTE_OPT_BUILT_IN_VULKAN_HEADER_TAG}")
endif()

if(TARGET Vulkan::Headers)
  set(KOMPUTE_OPT_USE_BUILT_IN_VULKAN_HEADER OFF CACHE BOOL "Use Vulkan headers from parent project if already provided." FORCE)
  message(STATUS "Using existing Vulkan::Headers target from parent project; disabling Kompute built-in Vulkan-Headers.")
endif()

set(KOMPUTE_OPT_DISABLE_VULKAN_VERSION_CHECK ON)

if(WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()
add_subdirectory(KOMPUTE)
