include(GoogleTest)

if(MSVC
   AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang"
   AND KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE)
  message(STATUS "Clang-CL detected: enabling raw libFuzzer targets for Windows.")

  # For raw libFuzzer, we don't need gtest/fuzztest libraries, we just compile with /fsanitize=fuzzer
  add_executable(raw_fuzzer raw_fuzzer.cpp)

  target_compile_options(raw_fuzzer PRIVATE /Z7 -fsanitize=fuzzer,address)
  target_link_options(raw_fuzzer PRIVATE -fsanitize=fuzzer,address)

  # Register the raw fuzzer as a ctest so `ctest` runs it
  # We restrict time/runs so ctest doesn't hang forever
  add_test(NAME raw_fuzzer COMMAND raw_fuzzer -runs=1000 -max_total_time=5)

elseif(
  CMAKE_SYSTEM_NAME STREQUAL "Linux"
  AND NOT (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  AND NOT (CMAKE_BUILD_TYPE STREQUAL "Release"))
  # Fuzz testing needs special treatment only in debug mode!
  if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    message(STATUS "Clang detected: enabling fuzz test targets.")

    add_executable(first_fuzz_test dummy.cpp)
    add_executable(example_fuzz_test example_fuzz_test.cpp)

    if(KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE OR (FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer"))
      if(MSVC)
        target_compile_options(
          first_fuzz_test
          PRIVATE /Z7
                  -UNDEBUG
                  -fsanitize=address
                  -fsanitize-coverage=inline-8bit-counters
                  -fsanitize-coverage=trace-cmp)
        target_compile_options(
          example_fuzz_test
          PRIVATE /Z7
                  -UNDEBUG
                  -fsanitize=address
                  -fsanitize-coverage=inline-8bit-counters
                  -fsanitize-coverage=trace-cmp)
      else()
        target_compile_options(
          first_fuzz_test
          PRIVATE -g
                  -UNDEBUG
                  -fsanitize=address
                  -fsanitize-coverage=inline-8bit-counters
                  -fsanitize-coverage=trace-cmp)
        target_compile_options(
          example_fuzz_test
          PRIVATE -g
                  -UNDEBUG
                  -fsanitize=address
                  -fsanitize-coverage=inline-8bit-counters
                  -fsanitize-coverage=trace-cmp)
      endif()
      target_compile_definitions(first_fuzz_test PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION ADDRESS_SANITIZER)
      target_link_options(first_fuzz_test PRIVATE -fsanitize=address)

      target_compile_definitions(example_fuzz_test PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION ADDRESS_SANITIZER)
      target_link_options(example_fuzz_test PRIVATE -fsanitize=address)
    endif()

    if(FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
      target_compile_options(first_fuzz_test PRIVATE -fsanitize=fuzzer-no-link)
      target_compile_definitions(first_fuzz_test PRIVATE FUZZTEST_COMPATIBILITY_MODE)

      target_compile_options(example_fuzz_test PRIVATE -fsanitize=fuzzer-no-link)
      target_compile_definitions(example_fuzz_test PRIVATE FUZZTEST_COMPATIBILITY_MODE)
    endif()

    link_fuzztest(first_fuzz_test)
    gtest_discover_tests(first_fuzz_test)

    link_fuzztest(example_fuzz_test)
    gtest_discover_tests(example_fuzz_test)
  endif()
endif()
