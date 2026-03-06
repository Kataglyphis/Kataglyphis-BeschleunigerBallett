function(
  myproject_enable_sanitizers
  project_name
  ENABLE_SANITIZER_ADDRESS
  ENABLE_SANITIZER_LEAK
  ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
  ENABLE_SANITIZER_THREAD
  ENABLE_SANITIZER_MEMORY)

  set(SANITIZERS "")

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    if(${ENABLE_SANITIZER_ADDRESS})
      list(APPEND SANITIZERS "address")
    endif()

    if(${ENABLE_SANITIZER_LEAK})
      list(APPEND SANITIZERS "leak")
    endif()

    if(${ENABLE_SANITIZER_UNDEFINED_BEHAVIOR})
      list(APPEND SANITIZERS "undefined")
    endif()

    if(${ENABLE_SANITIZER_THREAD})
      if("address" IN_LIST SANITIZERS OR "leak" IN_LIST SANITIZERS)
        message(WARNING "Thread sanitizer does not work with Address and Leak sanitizer enabled")
      else()
        list(APPEND SANITIZERS "thread")
      endif()
    endif()

    if(${ENABLE_SANITIZER_MEMORY} AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
      message(
        WARNING
          "Memory sanitizer requires all the code (including libc++) to be MSan-instrumented otherwise it reports false positives"
      )
      if("address" IN_LIST SANITIZERS
         OR "thread" IN_LIST SANITIZERS
         OR "leak" IN_LIST SANITIZERS)
        message(WARNING "Memory sanitizer does not work with Address, Thread or Leak sanitizer enabled")
      else()
        list(APPEND SANITIZERS "memory")
      endif()
    endif()
  elseif(MSVC)
    if(${ENABLE_SANITIZER_ADDRESS})
      list(APPEND SANITIZERS "address")
    endif()
    if(${ENABLE_SANITIZER_LEAK}
       OR ${ENABLE_SANITIZER_UNDEFINED_BEHAVIOR}
       OR ${ENABLE_SANITIZER_THREAD}
       OR ${ENABLE_SANITIZER_MEMORY})
      message(WARNING "MSVC only supports address sanitizer")
    endif()
  endif()

  list(
    JOIN
    SANITIZERS
    ","
    LIST_OF_SANITIZERS)

  if(LIST_OF_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang" AND MSVC)
      set(_CLANGCL_COMPILE_SAN_FLAGS "")
      set(_CLANGCL_LINK_SAN_FLAGS "")

      if("address" IN_LIST SANITIZERS)
        list(APPEND _CLANGCL_COMPILE_SAN_FLAGS /fsanitize=address)
      endif()

      if("undefined" IN_LIST SANITIZERS)
        list(APPEND _CLANGCL_COMPILE_SAN_FLAGS -fsanitize=undefined)
        list(APPEND _CLANGCL_LINK_SAN_FLAGS -fsanitize=undefined)
      endif()

      if("thread" IN_LIST SANITIZERS)
        message(
          WARNING
            "clang-cl ThreadSanitizer is not supported for target x86_64-pc-windows-msvc; ignoring thread sanitizer request"
        )
      endif()

      if("leak" IN_LIST SANITIZERS OR "memory" IN_LIST SANITIZERS)
        message(
          WARNING
            "clang-cl with MSVC ABI currently supports AddressSanitizer and UndefinedBehaviorSanitizer in this configuration"
        )
      endif()

      if(_CLANGCL_COMPILE_SAN_FLAGS)
        set(_CLANGCL_COMPILE_SAN_DEBUG_FLAGS "")
        foreach(_clangcl_flag IN LISTS _CLANGCL_COMPILE_SAN_FLAGS)
          list(APPEND _CLANGCL_COMPILE_SAN_DEBUG_FLAGS "$<$<CONFIG:Debug>:${_clangcl_flag}>")
        endforeach()

        set(_CLANGCL_LINK_SAN_DEBUG_FLAGS "")
        foreach(_clangcl_flag IN LISTS _CLANGCL_LINK_SAN_FLAGS)
          list(APPEND _CLANGCL_LINK_SAN_DEBUG_FLAGS "$<$<CONFIG:Debug>:${_clangcl_flag}>")
        endforeach()

        target_compile_options(${project_name} INTERFACE ${_CLANGCL_COMPILE_SAN_DEBUG_FLAGS} "$<$<CONFIG:Debug>:/Zi>")
        target_link_options(
          ${project_name}
          INTERFACE
          "$<$<CONFIG:Debug>:/INCREMENTAL:NO>"
          ${_CLANGCL_LINK_SAN_DEBUG_FLAGS})
      endif()

      if("address" IN_LIST SANITIZERS)
        target_compile_definitions(${project_name} INTERFACE "$<$<CONFIG:Debug>:_DISABLE_VECTOR_ANNOTATION>"
                                                             "$<$<CONFIG:Debug>:_DISABLE_STRING_ANNOTATION>")

        execute_process(
          COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
          OUTPUT_VARIABLE _CLANG_RESOURCE_DIR
          OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_CLANG_RESOURCE_DIR)
          set(_CLANG_RUNTIME_DIR "${_CLANG_RESOURCE_DIR}/lib/windows")
          set(_ASAN_DYNAMIC_LIB "${_CLANG_RUNTIME_DIR}/clang_rt.asan_dynamic-x86_64.lib")
          set(_ASAN_THUNK_LIB "${_CLANG_RUNTIME_DIR}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
          if(EXISTS "${_ASAN_DYNAMIC_LIB}" AND EXISTS "${_ASAN_THUNK_LIB}")
            target_link_directories(${project_name} INTERFACE "${_CLANG_RUNTIME_DIR}")
            target_link_libraries(
              ${project_name} INTERFACE "$<$<CONFIG:Debug>:clang_rt.asan_dynamic-x86_64>"
                                        "$<$<CONFIG:Debug>:clang_rt.asan_dynamic_runtime_thunk-x86_64>")
            target_link_options(${project_name} INTERFACE
                                "$<$<CONFIG:Debug>:/WHOLEARCHIVE:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib>")
          else()
            message(WARNING "clang-cl ASan runtime libraries not found in ${_CLANG_RUNTIME_DIR}")
          endif()
        else()
          message(WARNING "Unable to detect clang resource directory for clang-cl ASan runtime linkage")
        endif()
      endif()
    elseif(MSVC)
      string(FIND "$ENV{PATH}" "$ENV{VSINSTALLDIR}" index_of_vs_install_dir)
      if("${index_of_vs_install_dir}" STREQUAL "-1")
        message(
          SEND_ERROR
            "Using MSVC sanitizers requires setting the MSVC environment before building the project. Please manually open the MSVC command prompt and rebuild the project."
        )
      endif()
      target_compile_options(${project_name} INTERFACE "$<$<CONFIG:Debug>:/fsanitize=${LIST_OF_SANITIZERS}>"
                                                       "$<$<CONFIG:Debug>:/Zi>")
      target_compile_definitions(${project_name} INTERFACE "$<$<CONFIG:Debug>:_DISABLE_VECTOR_ANNOTATION>"
                                                           "$<$<CONFIG:Debug>:_DISABLE_STRING_ANNOTATION>")
      target_link_options(${project_name} INTERFACE "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    else()
      target_compile_options(${project_name} INTERFACE "$<$<CONFIG:Debug>:-fsanitize=${LIST_OF_SANITIZERS}>")
      target_link_options(${project_name} INTERFACE "$<$<CONFIG:Debug>:-fsanitize=${LIST_OF_SANITIZERS}>")
    endif()
  endif()

endfunction()
