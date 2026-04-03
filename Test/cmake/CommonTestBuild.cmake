function(kataglyphis_configure_gtest_discovery test_target)
  if(NOT DEFINED KATAGLYPHIS_ENABLE_GTEST_DISCOVERY)
    set(KATAGLYPHIS_ENABLE_GTEST_DISCOVERY ON)
  endif()

  if(KATAGLYPHIS_ENABLE_GTEST_DISCOVERY)
    message(STATUS "Enabling gtest_discover_tests for ${test_target}.")
    # PRE_TEST keeps test discovery inside the ctest phase, which is safer for
    # Windows runtime-path handling than executing tests during build steps.
    # WORKING_DIRECTORY is set to project root so relative paths match the main executable.
    gtest_discover_tests(
      ${test_target}
      DISCOVERY_TIMEOUT
      300
      DISCOVERY_MODE
      PRE_TEST
      WORKING_DIRECTORY
      ${CMAKE_SOURCE_DIR})
  else()
    message(STATUS "KATAGLYPHIS_ENABLE_GTEST_DISCOVERY is OFF - skipping gtest_discover_tests for ${test_target}.")
  endif()
endfunction()
