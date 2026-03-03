include(GoogleTest)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang"
   AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "Linux + Clang + Debug detected: enabling fuzz test targets.")

  add_executable(first_fuzz_test dummy.cpp)

  if(KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE OR (FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer"))
    target_compile_options(
      first_fuzz_test
      PRIVATE -g
              -UNDEBUG
              -fsanitize=address
              -fsanitize-coverage=inline-8bit-counters
              -fsanitize-coverage=trace-cmp)
    target_compile_definitions(
      first_fuzz_test
      PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
              ADDRESS_SANITIZER)
    target_link_options(first_fuzz_test PRIVATE -fsanitize=address)
  endif()

  if(FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
    target_compile_options(first_fuzz_test PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_definitions(first_fuzz_test PRIVATE FUZZTEST_COMPATIBILITY_MODE)
  endif()

  link_fuzztest(first_fuzz_test)
  gtest_discover_tests(first_fuzz_test)
endif()
