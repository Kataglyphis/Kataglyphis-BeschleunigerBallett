set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../)

if(NOT
   CMAKE_BUILD_TYPE
   STREQUAL
   "RelWithDebInfo")
  message(STATUS "Skipping perf tests: build type is '${CMAKE_BUILD_TYPE}', requires RelWithDebInfo.")
  return()
endif()

include(cmake/SetTestFilters.cmake)

set(PERF_TEST_SUITE perfTestSuite)

add_executable(${PERF_TEST_SUITE})

# Enable C++20 module scanning so benchmark sources can import modules from VulkanEngineCore.
set_target_properties(${PERF_TEST_SUITE} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

target_sources(
  ${PERF_TEST_SUITE}
  PRIVATE ${RENDERER_PERF_TEST_FILTER}
          # IMGUI object library - excluded from static analysis
          $<TARGET_OBJECTS:IMGUI>)

target_link_libraries(
  ${PERF_TEST_SUITE}
  PRIVATE VulkanEngineCore
          benchmark::benchmark)

# disable all warnings for our test suite
if(MSVC)
  target_compile_options(${PERF_TEST_SUITE} PRIVATE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(${PERF_TEST_SUITE} PRIVATE -w)
else()
  target_compile_options(${PERF_TEST_SUITE} PRIVATE -w)
endif()
