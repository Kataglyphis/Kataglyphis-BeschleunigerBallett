if(NOT
   CMAKE_BUILD_TYPE
   STREQUAL
   "Debug")
  message(STATUS "Skipping fuzz tests: build type is '${CMAKE_BUILD_TYPE}', requires Debug.")
  return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  message(STATUS "Skipping fuzz tests: requires Clang or clang-cl.")
  return()
endif()

if(NOT(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR (WIN32 AND MSVC AND KATAGLYPHIS_ENABLE_FUZZTEST_FUZZING_MODE)))
  message(STATUS "Skipping fuzz tests: supported on Linux Clang and Windows clang-cl fuzzing mode.")
  return()
endif()

message(STATUS "Clang detected: enabling fuzz test targets.")

include(${CMAKE_SOURCE_DIR}/Test/cmake/CommonTestBuild.cmake)

function(kataglyphis_add_fuzz_test fuzz_target source_file)
  add_executable(${fuzz_target} ${source_file})

  # Fuzz targets that import engine modules need module scanning enabled,
  # exactly like the unit-test targets (see Test/cmake/CommonTestBuild.cmake).
  # Without it clang reports: module 'kataglyphis.vulkan.*' not found.
  set_target_properties(${fuzz_target} PROPERTIES CXX_SCAN_FOR_MODULES ${MYPROJECT_CXX_SCAN_FOR_MODULES})

  if(MSVC)
    target_link_libraries(${fuzz_target} PRIVATE kataglyphis_fuzztest_windows_asan)

    target_compile_options(
      ${fuzz_target}
      PRIVATE /Z7
              -UNDEBUG
              -fsanitize-coverage=inline-8bit-counters
              -fsanitize-coverage=trace-cmp)
  else()
    target_compile_options(
      ${fuzz_target}
      PRIVATE -g
              -UNDEBUG
              -fsanitize=address
              -fsanitize-coverage=inline-8bit-counters
              -fsanitize-coverage=trace-cmp)
  endif()

  target_compile_definitions(
    ${fuzz_target}
    PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
            ADDRESS_SANITIZER)

  if(NOT MSVC)
    target_link_options(${fuzz_target} PRIVATE -fsanitize=address)
  endif()

  if(FUZZTEST_COMPATIBILITY_MODE STREQUAL "libfuzzer")
    target_compile_options(${fuzz_target} PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_definitions(${fuzz_target} PRIVATE FUZZTEST_COMPATIBILITY_MODE)
  endif()

  link_fuzztest(${fuzz_target})
  kataglyphis_configure_gtest_discovery(${fuzz_target})
endfunction()

kataglyphis_add_fuzz_test(first_fuzz_test dummy.cpp)
kataglyphis_add_fuzz_test(example_fuzz_test example_fuzz_test.cpp)
# Real engine surface: OBJ parsing (mirrors ObjLoader::loadVertices).
kataglyphis_add_fuzz_test(obj_parsing_fuzz_test obj_parsing_fuzz_test.cpp)
target_link_libraries(obj_parsing_fuzz_test PRIVATE tinyobjloader)

# Real engine surface: scene-config path resolution (GUI model selection).
kataglyphis_add_fuzz_test(scene_config_fuzz_test scene_config_fuzz_test.cpp)
target_link_libraries(scene_config_fuzz_test PRIVATE VulkanEngineCore)

# Real engine surface: the file reader every shader load goes through
# (ShaderHelper -> File::readCharSequence -> VkShaderModule, unvalidated).
kataglyphis_add_fuzz_test(shader_file_reader_fuzz_test shader_file_reader_fuzz_test.cpp)
target_link_libraries(shader_file_reader_fuzz_test PRIVATE VulkanEngineCore)
