include(GoogleTest)

function(kataglyphis_disable_test_warnings test_target)
  if(MSVC)
    target_compile_options(${test_target} PRIVATE /w)
  else()
    target_compile_options(${test_target} PRIVATE -w)
  endif()
endfunction()

function(kataglyphis_configure_common_test_target test_target)
  set_target_properties(${test_target} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})
  kataglyphis_disable_test_warnings(${test_target})
endfunction()

function(kataglyphis_add_imgui_test_sources test_target)
  target_sources(
    ${test_target}
    PRIVATE
      # IMGUI object library - excluded from static analysis
      $<TARGET_OBJECTS:IMGUI>)
endfunction()

function(kataglyphis_enable_windows_vulkan_delay_load test_target)
  if(WIN32 AND MSVC)
    target_link_options(${test_target} PRIVATE /DELAYLOAD:vulkan-1.dll)
    target_link_libraries(${test_target} PRIVATE delayimp)
  endif()
endfunction()

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
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  else()
    message(STATUS "KATAGLYPHIS_ENABLE_GTEST_DISCOVERY is OFF - skipping gtest_discover_tests for ${test_target}.")
  endif()
endfunction()
