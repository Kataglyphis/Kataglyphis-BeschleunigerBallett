include(GoogleTest)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(STATUS "This is a Linux system.")

  if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")

    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
      fuzztest_setup_fuzzing_flags()
      # Remove AddressSanitizer flags if disabled
      string(
        REPLACE "-fsanitize=address"
                ""
                CMAKE_C_FLAGS
                "${CMAKE_C_FLAGS}")
      string(
        REPLACE "-fsanitize=address"
                ""
                CMAKE_CXX_FLAGS
                "${CMAKE_CXX_FLAGS}")
      string(
        REPLACE "-fsanitize=address"
                ""
                CMAKE_EXE_LINKER_FLAGS
                "${CMAKE_EXE_LINKER_FLAGS}")
      remove_definitions(-DADDRESS_SANITIZER)
    endif()

    add_executable(first_fuzz_test dummy.cpp)

    link_fuzztest(first_fuzz_test)
    gtest_discover_tests(first_fuzz_test)
  endif()
endif()
