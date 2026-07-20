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

# On Linux the fuzz targets require ASan to be enabled for the WHOLE project,
# not just for themselves.
#
# This is not a preference. Abseil's raw_hash_set layout depends on whether
# ASan is active, so instrumenting only the fuzz executable while FuzzTest's
# abseil is built without it is an ODR violation, and the binary dies during
# startup before reaching a single test. It cost this repository the Linux
# lane from 2026-05-17 to 2026-07-20, presenting as a bare
# "SEGV on unknown address 0x000000000000" in CI.
#
# Reproduced in the CI image, same source, same compiler, ONE variable:
#
#   ASan on the fuzz target only -> raw_hash_set.h:1016 assertion
#                                   "Try enabling sanitizers." / SEGV
#   ASan on every TU             -> 2 tests PASSED
#
# So: build with preset linux-debug-asan-clang (myproject_ENABLE_SANITIZER_ADDRESS
# ON) and run the targets from that build directory. Windows already solves the
# same problem differently, via the kataglyphis_fuzztest_windows_asan interface
# library in ExternalLib/CMakeLists.txt, which propagates the flags to abseil,
# re2 and the fuzztest_* targets by hand.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT myproject_ENABLE_SANITIZER_ADDRESS)
  message(
    STATUS
      "Skipping fuzz tests: they need myproject_ENABLE_SANITIZER_ADDRESS=ON so "
      "abseil and the fuzz targets agree on container layout. Use the "
      "linux-debug-asan-clang preset.")
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
    # NOTE: no -fsanitize=address here on purpose. It arrives project-wide from
    # the ASan preset (enforced above), so abseil and this target agree.
    # Setting it per target is exactly the ODR bug described at the top.
    target_compile_options(
      ${fuzz_target}
      PRIVATE -g
              -UNDEBUG
              -fsanitize-coverage=inline-8bit-counters
              -fsanitize-coverage=trace-cmp)
  endif()

  target_compile_definitions(
    ${fuzz_target}
    PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
            ADDRESS_SANITIZER)



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

# Real engine surface: texture decoding (stb_image). Every material texture and
# every skybox face goes through it, and its reported byte count sizes a
# staging buffer that is then memcpy'd into.
kataglyphis_add_fuzz_test(texture_loading_fuzz_test texture_loading_fuzz_test.cpp)
target_link_libraries(texture_loading_fuzz_test PRIVATE VulkanEngineCore)
# Importing the texture module drags in the renderer's ImGui references, unlike
# the lighter modules the other fuzz targets use.
kataglyphis_add_imgui_test_sources(texture_loading_fuzz_test)
