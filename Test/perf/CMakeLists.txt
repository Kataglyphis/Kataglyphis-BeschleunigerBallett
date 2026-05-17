if(NOT
   CMAKE_BUILD_TYPE
   STREQUAL
   "RelWithDebInfo")
  message(STATUS "Skipping perf tests: build type is '${CMAKE_BUILD_TYPE}', requires RelWithDebInfo.")
  return()
endif()

include(${CMAKE_SOURCE_DIR}/Test/cmake/CommonTestBuild.cmake)
include(cmake/SetTestFilters.cmake)

set(PERF_TEST_SUITE perfTestSuite)

add_executable(${PERF_TEST_SUITE})

kataglyphis_configure_common_test_target(${PERF_TEST_SUITE})

target_sources(${PERF_TEST_SUITE} PRIVATE ${RENDERER_PERF_TEST_FILTER})
kataglyphis_add_imgui_test_sources(${PERF_TEST_SUITE})

target_link_libraries(
  ${PERF_TEST_SUITE}
  PRIVATE VulkanEngineCore
          benchmark::benchmark)
