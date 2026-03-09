# https://cmake.org/cmake/help/latest/module/ProcessorCount.html

include(ProcessorCount)
# 1. Detect number of cores
ProcessorCount(N)
message(STATUS "Processor count: ${N}") # prints “-- Processor count: <N>” during configure
if(NOT
   N
   EQUAL
   0)
  set(CTEST_BUILD_FLAGS -j${N})
  set(ctest_test_args ${ctest_test_args} PARALLEL_LEVEL ${N})
  # 2. Export as environment variable
  set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} "${N}")
endif()
