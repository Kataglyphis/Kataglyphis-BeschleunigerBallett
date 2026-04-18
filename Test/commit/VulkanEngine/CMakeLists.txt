include(GoogleTest)

# all testing related stuff
set(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../)

include(${WORKING_DIRECTORY}Test/cmake/CommonTestBuild.cmake)

set(COMMIT_TEST_SUITE commitTestSuite)

# Only the test sources - NOT the VulkanEngine sources
file(GLOB RENDERER_COMMIT_TEST_FILTER "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(MYPROJECT_CMAKE_DEBUG_LOG)
  message(STATUS "Render commit test filter is: ${RENDERER_COMMIT_TEST_FILTER}")
else()
  message(VERBOSE "Render commit test filter is: ${RENDERER_COMMIT_TEST_FILTER}")
endif()

add_executable(${COMMIT_TEST_SUITE})

# Enable C++20 module scanning so we can import modules from VulkanEngineCore
set_target_properties(${COMMIT_TEST_SUITE} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

# Only add test sources and IMGUI objects - NOT VulkanEngineCore sources
target_sources(
  ${COMMIT_TEST_SUITE}
  PRIVATE ${RENDERER_COMMIT_TEST_FILTER}
          # IMGUI object library - excluded from static analysis
          $<TARGET_OBJECTS:IMGUI>)

# Link against VulkanEngineCore static library
# This brings in all necessary libraries, includes, and module BMIs
target_link_libraries(${COMMIT_TEST_SUITE} PRIVATE VulkanEngineCore gtest gtest_main)

# Use the project root as working directory so relative paths match the main executable
kataglyphis_configure_gtest_discovery(${COMMIT_TEST_SUITE})

# disable all warnings for our test suite
if(MSVC)
  target_compile_options(${COMMIT_TEST_SUITE} PRIVATE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(${COMMIT_TEST_SUITE} PRIVATE -w)
else()
  target_compile_options(${COMMIT_TEST_SUITE} PRIVATE -w)
endif()
