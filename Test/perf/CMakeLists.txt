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
          benchmark::benchmark
          tinyobjloader
          cgltf)

# Register with CTest so the benchmarks are at least RUN by `ctest`. Until now
# they were built and then never executed by anything, so a benchmark that
# crashed or failed to converge went unnoticed.
#
# This deliberately gates on "the benchmarks execute successfully", NOT on a
# time budget. Wall-clock numbers here depend on the machine, its thermal
# state and what else is running; a threshold would either be so loose it
# catches nothing or so tight it fails randomly. Regression tracking belongs
# in the JSON below, compared against a per-machine baseline - see BACKLOG.md.
#
# --benchmark_min_time keeps the ctest run short; the recorded baseline in
# BACKLOG.md was taken without it.
add_test(
  NAME perf_suite_runs
  COMMAND
    ${PERF_TEST_SUITE} --benchmark_min_time=0.05s
    --benchmark_out=${CMAKE_BINARY_DIR}/perf-results.json
    --benchmark_out_format=json
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Benchmarks touching the GPU or large assets are slower than unit tests, and
# this suite only exists in RelWithDebInfo.
set_tests_properties(perf_suite_runs PROPERTIES TIMEOUT 900 LABELS "perf")
