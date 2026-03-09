set(MYPROJECT_CLANG_CL_MODULE_SCAN_READY OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16.0 AND CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    if(CMAKE_CXX_COMPILER_CLANG_RESOURCE_DIR)
      set(_KATAGLYPHIS_CLANG_SCAN_RESOURCE_DIR " -resource-dir \"${CMAKE_CXX_COMPILER_CLANG_RESOURCE_DIR}\"")
    else()
      set(_KATAGLYPHIS_CLANG_SCAN_RESOURCE_DIR "")
    endif()

    # Keep the dyndep output stable if the scan command fails midway.
    if(CMAKE_HOST_WIN32)
      set(_KATAGLYPHIS_CLANG_SCAN_MOVE "\"${CMAKE_COMMAND}\" -E rename")
    else()
      set(_KATAGLYPHIS_CLANG_SCAN_MOVE "mv")
    endif()

    string(
      CONCAT CMAKE_CXX_SCANDEP_SOURCE
             "\"${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}\""
             " -format=p1689"
             " --"
             " <CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS>"
             " -x c++ <SOURCE> -c -o <OBJECT>"
             "${_KATAGLYPHIS_CLANG_SCAN_RESOURCE_DIR}"
             " > <DYNDEP_FILE>.tmp"
             " && ${_KATAGLYPHIS_CLANG_SCAN_MOVE} <DYNDEP_FILE>.tmp <DYNDEP_FILE>")

    set(CMAKE_CXX_MODULE_MAP_FORMAT "clang")
    set(CMAKE_CXX_MODULE_MAP_FLAG "@<MODULE_MAP_FILE>")
    set(CMAKE_CXX_MODULE_BMI_ONLY_FLAG "--precompile")
    set(MYPROJECT_CLANG_CL_MODULE_SCAN_READY ON)

    message(STATUS "Enabled clang-cl module scanning via project override.")
  else()
    message(WARNING "clang-cl detected, but clang-scan-deps is unavailable; C++ module scanning stays disabled.")
  endif()
endif()
