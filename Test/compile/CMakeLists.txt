include(GoogleTest)

if(NOT
   CMAKE_BUILD_TYPE
   STREQUAL
   "Debug")
  message(STATUS "Skipping compile tests: build type is '${CMAKE_BUILD_TYPE}', requires Debug.")
  return()
endif()

set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../)

include(${WORKING_DIRECTORY}Test/cmake/CommonTestBuild.cmake)

# Only the test sources - NOT the VulkanEngine sources
file(GLOB RENDERER_COMPILE_TEST_FILTER "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(MYPROJECT_CMAKE_DEBUG_LOG)
  message(STATUS "Render compile test suite files are: ${RENDERER_COMPILE_TEST_FILTER}")
else()
  message(VERBOSE "Render compile test suite files are: ${RENDERER_COMPILE_TEST_FILTER}")
endif()

set(COMPILE_TEST_SUITE compileTestSuite)

add_executable(${COMPILE_TEST_SUITE})

# Enable C++20 module scanning so we can import modules from VulkanEngineCore
set_target_properties(${COMPILE_TEST_SUITE} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

# Only add test sources and IMGUI objects - NOT VulkanEngineCore sources
target_sources(
  ${COMPILE_TEST_SUITE}
  PRIVATE ${RENDERER_COMPILE_TEST_FILTER}
          # IMGUI object library - excluded from static analysis
          $<TARGET_OBJECTS:IMGUI>)

# Link against VulkanEngineCore static library
# This brings in all necessary libraries, includes, and module BMIs
target_link_libraries(${COMPILE_TEST_SUITE} PRIVATE VulkanEngineCore gtest_main gtest)

if(WIN32 AND MSVC)
  # Delay-load the Vulkan loader so basic gtest discovery does not require the
  # system Vulkan runtime to be installed up front.
  target_link_options(${COMPILE_TEST_SUITE} PRIVATE /DELAYLOAD:vulkan-1.dll)
  target_link_libraries(${COMPILE_TEST_SUITE} PRIVATE delayimp)
endif()

# Use the project root as working directory so relative paths match the main executable
kataglyphis_configure_gtest_discovery(${COMPILE_TEST_SUITE})

# disable all warnings for our test suite
if(MSVC)
  target_compile_options(${COMPILE_TEST_SUITE} PRIVATE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(${COMPILE_TEST_SUITE} PRIVATE -w)
else()
  target_compile_options(${COMPILE_TEST_SUITE} PRIVATE -w)
endif()
