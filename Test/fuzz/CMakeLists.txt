include(GoogleTest)

if(NOT
   CMAKE_BUILD_TYPE
   STREQUAL
   "Debug")
  message(STATUS "Skipping fuzz tests: build type is '${CMAKE_BUILD_TYPE}', requires Debug.")
  return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  message(STATUS "Skipping fuzz tests: requires Clang or clang-cl.")
  return()
endif()

if(NOT(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR (WIN32 AND MSVC AND KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE)))
  message(STATUS "Skipping fuzz tests: supported on Linux Clang and Windows clang-cl fuzzing mode.")
  return()
endif()

message(STATUS "Clang detected: enabling fuzz test targets.")

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CommonTestBuild.cmake)

add_executable(first_fuzz_test dummy.cpp)
add_executable(example_fuzz_test example_fuzz_test.cpp)

set(_KATAGLYPHIS_FUZZ_TEST_TARGETS first_fuzz_test example_fuzz_test)

foreach(_kataglyphis_fuzz_target IN LISTS _KATAGLYPHIS_FUZZ_TEST_TARGETS)
  if(MSVC)
    target_link_libraries(${_kataglyphis_fuzz_target} PRIVATE kataglyphis_fuzztest_windows_asan)

    target_compile_options(
      ${_kataglyphis_fuzz_target}
      PRIVATE /Z7
              -UNDEBUG
              -fsanitize-coverage=inline-8bit-counters
              -fsanitize-coverage=trace-cmp)
  else()
    target_compile_options(
      ${_kataglyphis_fuzz_target}
      PRIVATE -g
              -UNDEBUG
              -fsanitize=address
              -fsanitize-coverage=inline-8bit-counters
              -fsanitize-coverage=trace-cmp)
  endif()

  target_compile_definitions(
    ${_kataglyphis_fuzz_target}
    PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
            ADDRESS_SANITIZER)

  if(NOT MSVC)
    target_link_options(${_kataglyphis_fuzz_target} PRIVATE -fsanitize=address)
  endif()

  if(FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
    target_compile_options(${_kataglyphis_fuzz_target} PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_definitions(${_kataglyphis_fuzz_target} PRIVATE FUZZTEST_COMPATIBILITY_MODE)
  endif()

  link_fuzztest(${_kataglyphis_fuzz_target})
  kataglyphis_configure_gtest_discovery(${_kataglyphis_fuzz_target})
endforeach()
