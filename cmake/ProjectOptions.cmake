# This project's build POLICY: which options exist, what they default to, and
# how the reusable modules are composed.
#
# The reusable mechanisms this file used to carry inline were upstreamed to
# ContainerHub on 2026-08-07 and are included by name off CMAKE_MODULE_PATH
# (see the top of the root CMakeLists.txt):
#
#   SanitizerSupport    myproject_supports_sanitizers, myproject_default_debug_sanitizers
#   CompilerBuildFlags  myproject_apply_compiler_build_flags + the flag-strip helpers,
#                       including the clang-cl -fms-compatibility-version pin, which
#                       now lives next to the image whose VC Tools version it names
#
# What stays here is what another project would NOT want copied: exceptions
# always off, C++23, C++ modules mandatory (a hard FATAL_ERROR, not a fallback),
# cppcheck and IWYU on by default.

include(CMakeDependentOption)
include(CheckCXXCompilerFlag)

include(SanitizerSupport)
include(CompilerBuildFlags)

macro(myproject_define_core_options)
  option(myproject_ENABLE_IPO "Enable IPO/LTO" ON)
  option(myproject_ENABLE_STATIC_ANALYZER "Enable Static Analyzer" OFF)
  option(myproject_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
  option(myproject_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${DEFAULT_ASAN})
  option(myproject_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
  option(myproject_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${DEFAULT_UBSAN})
  option(myproject_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
  option(myproject_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
  option(myproject_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
  option(myproject_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
  option(myproject_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
  option(myproject_ENABLE_PCH "Enable precompiled headers" OFF)
  option(myproject_ENABLE_CACHE "Enable ccache" ON)
  option(myproject_ENABLE_IWYU "Enable IWYU" ON)
endmacro()

function(myproject_enable_local_hardening target)
  include(Hardening)
  # Current project behavior always keeps the UBSan minimal runtime disabled,
  # even though older logic computed a value first.
  set(_MYPROJECT_ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
  myproject_enable_hardening(${target} OFF ${_MYPROJECT_ENABLE_UBSAN_MINIMAL_RUNTIME})
endfunction()

macro(myproject_setup_options)
  option(myproject_ENABLE_HARDENING "Enable hardening" ON)
  option(myproject_ENABLE_COVERAGE "Enable coverage reporting" ON)
  option(myproject_ENABLE_GPROF "Enable profiling with gprof (adds -pg flags)" ON)
  option(myproject_ENABLE_GLOBAL_HARDENING "Enable global hardening" OFF)
  # Exceptions are always disabled for consistent behavior across all builds
  # This avoids /EHs vs /EHs- conflicts and reduces binary size
  # turn off for avoiding potential conflicts with dependencies
  # cmake_dependent_option(
  #   myproject_ENABLE_GLOBAL_HARDENING
  #   "Attempt to push hardening options to built dependencies"
  #   OFF
  #   myproject_ENABLE_HARDENING
  #   OFF)

  myproject_supports_sanitizers()
  myproject_default_debug_sanitizers()

  myproject_define_core_options()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      myproject_ENABLE_IPO
      myproject_ENABLE_STATIC_ANALYZER
      myproject_WARNINGS_AS_ERRORS
      myproject_ENABLE_SANITIZER_ADDRESS
      myproject_ENABLE_SANITIZER_LEAK
      myproject_ENABLE_SANITIZER_UNDEFINED
      myproject_ENABLE_SANITIZER_THREAD
      myproject_ENABLE_SANITIZER_MEMORY
      myproject_ENABLE_UNITY_BUILD
      myproject_ENABLE_CLANG_TIDY
      myproject_ENABLE_CPPCHECK
      myproject_ENABLE_COVERAGE
      myproject_ENABLE_PCH
      myproject_ENABLE_CACHE)
  endif()

endmacro()

macro(myproject_global_options)

  # specify the C/C++ standard
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED True)

  set(CMAKE_C_STANDARD 17)
  set(CMAKE_C_STANDARD_REQUIRED True)

  set(myproject_CXX_SCAN_FOR_MODULES ON)
  set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # Enable C++20 modules for clang-cl by default (requires Clang >= 17)
    # Set MYPROJECT_CLANG_CL_MODULE_SCAN_READY=OFF to disable if issues arise
    if(DEFINED MYPROJECT_CLANG_CL_MODULE_SCAN_READY AND NOT MYPROJECT_CLANG_CL_MODULE_SCAN_READY)
      set(myproject_CXX_SCAN_FOR_MODULES OFF)
      message(STATUS "C++ module scanning explicitly disabled for clang-cl.")
    else()
      set(myproject_CXX_SCAN_FOR_MODULES ON)
      message(STATUS "C++ module scanning enabled for clang-cl.")
    endif()
  endif()

  set(MYPROJECT_CXX_SCAN_FOR_MODULES
      ${myproject_CXX_SCAN_FOR_MODULES}
      CACHE INTERNAL "Global C++ module scan switch for project targets")

  message(STATUS "Global C++ modules scan disabled; project targets can opt in explicitly.")

  set(myproject_CPP_MODULES_SUPPORTED OFF)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    # Clang family (incl. AppleClang, clang-cl)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*")
      if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 17)
        set(myproject_CPP_MODULES_SUPPORTED ON)
      endif()
      # MSVC (excluding clang-cl which is handled above)
    elseif(MSVC)
      if(MSVC_VERSION GREATER_EQUAL 1934)
        set(myproject_CPP_MODULES_SUPPORTED ON)
      endif()
      # GCC
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # CMake's module scanning support for GCC is still evolving; keep a conservative floor.
      if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
        set(myproject_CPP_MODULES_SUPPORTED ON)
      endif()
    endif()
  endif()

  if(NOT myproject_CPP_MODULES_SUPPORTED)
    message(FATAL_ERROR "This project is configured for C++ modules only. "
                        "Use a module-capable toolchain (Clang >= 17, GCC >= 14, or MSVC >= 19.34 with CMake >= 3.28).")
  endif()

  set(myproject_USE_CPP_MODULES ON)
  message(STATUS "C++ modules are enabled for compiler '${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}'.")

  # set build type specific flags (upstream: CompilerBuildFlags)
  myproject_apply_compiler_build_flags(${myproject_ENABLE_SANITIZER_ADDRESS})

  # control where the static and shared libraries are built so that on windows
  # we don't need to tinker with the path to run the executable
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
     AND MSVC
     AND myproject_ENABLE_SANITIZER_ADDRESS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_LINK_WHAT_YOU_USE FALSE)
  else()
    set(CMAKE_LINK_WHAT_YOU_USE TRUE)
  endif()

  if(myproject_ENABLE_IPO)
    include(InterproceduralOptimization)
    if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
      myproject_enable_ipo()
    endif()
  endif()

  myproject_supports_sanitizers()

  if(myproject_ENABLE_HARDENING AND myproject_ENABLE_GLOBAL_HARDENING)
    include(Hardening)
    set(_MYPROJECT_ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    message("${myproject_ENABLE_HARDENING} ${_MYPROJECT_ENABLE_UBSAN_MINIMAL_RUNTIME} ${myproject_ENABLE_SANITIZER_UNDEFINED}")
    myproject_enable_hardening(myproject_options ON ${_MYPROJECT_ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(myproject_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(StandardProjectSettings)
  endif()

  add_library(myproject_warnings INTERFACE)
  add_library(myproject_options INTERFACE)

  target_compile_features(myproject_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

  include(CompilerWarnings)
  myproject_set_project_warnings(
    myproject_warnings
    ${myproject_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  # Only when building with -DCMAKE_BUILD_TYPE=RelWithDebInfo,
  # on non-Windows and using GCC or Clang
  if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"
     AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
     AND NOT WIN32)

    find_library(PROFILER_LIB profiler)

    if(PROFILER_LIB)
      message(STATUS "Enabling CPU profiling with gperftools (libprofiler)")
      message(STATUS "Found libprofiler: ${PROFILER_LIB}")
      target_link_libraries(myproject_options INTERFACE ${PROFILER_LIB})
    else()
      message(MESSAGE "libprofiler not found, falling back to gprof (-pg)")
      target_compile_options(myproject_options INTERFACE -pg)
      target_link_libraries(myproject_options INTERFACE -pg)
    endif()
  endif()

  # Always disable C++ exceptions - /EHs- for MSVC, -fno-exceptions for GCC/Clang
  if(MSVC)
    target_compile_options(myproject_options INTERFACE /EHs-)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(myproject_options INTERFACE -fno-exceptions)
  else()
    message(WARNING "Disabling exceptions is not supported for this compiler.")
  endif()

  include(Sanitizers)
  myproject_enable_sanitizers(
    myproject_options
    ${myproject_ENABLE_SANITIZER_ADDRESS}
    ${myproject_ENABLE_SANITIZER_LEAK}
    ${myproject_ENABLE_SANITIZER_UNDEFINED}
    ${myproject_ENABLE_SANITIZER_THREAD}
    ${myproject_ENABLE_SANITIZER_MEMORY})

  set_target_properties(myproject_options PROPERTIES UNITY_BUILD ${myproject_ENABLE_UNITY_BUILD})

  if(myproject_ENABLE_PCH)
    target_precompile_headers(
      myproject_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(myproject_ENABLE_CACHE)
    include(Cache)
    myproject_enable_cache()
  endif()

  if(NOT
     CMAKE_BUILD_TYPE
     STREQUAL
     "Release")
    include(StaticAnalyzers)
    if(myproject_ENABLE_CLANG_TIDY)
      myproject_enable_clang_tidy(myproject_options ${myproject_WARNINGS_AS_ERRORS})
    endif()
    if(myproject_ENABLE_CPPCHECK)
      myproject_enable_cppcheck(${myproject_WARNINGS_AS_ERRORS} "")
    endif()
    if(myproject_ENABLE_COVERAGE)
      include(Tests)
      myproject_enable_coverage(myproject_options)
    endif()
  endif()

  if(myproject_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(myproject_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(myproject_ENABLE_HARDENING AND NOT myproject_ENABLE_GLOBAL_HARDENING)
    myproject_enable_local_hardening(myproject_options)
  endif()

  if(NOT
     CMAKE_BUILD_TYPE
     STREQUAL
     "Release")
    if(myproject_ENABLE_IWYU)
      if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        find_program(IWYU_PATH NAMES include-what-you-use iwyu)
        if(IWYU_PATH)
          set_target_properties(myproject_options PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${IWYU_PATH}")
          message(STATUS "Include-What-You-Use found: ${IWYU_PATH}")
        else()
          message(STATUS "Include-What-You-Use not found!")
        endif()
      endif()
    endif()
  endif()

  include(Doxygen)
  enable_doxygen()

  if(myproject_ENABLE_STATIC_ANALYZER)
    if(MSVC)
      target_compile_options(${project_name} INTERFACE /analyze)
      target_link_libraries(${project_name} INTERFACE /analyze)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(myproject_options INTERFACE -fanalyzer)
      target_link_libraries(myproject_options INTERFACE -fanalyzer)
      # https://clang.llvm.org/docs/UsersManual.html
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
      #target_compile_options(myproject_options INTERFACE --analyze)
      #target_link_libraries(myproject_options INTERFACE --analyze)
      # https://clang.llvm.org/docs/ClangCommandLineReference.html
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      #target_compile_options(myproject_options INTERFACE --analyze --analyzer-output html)
      #target_link_libraries(myproject_options INTERFACE --analyze --analyzer-output html)
    endif()
  endif()

  include(Speedup)

endmacro()
