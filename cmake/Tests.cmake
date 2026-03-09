function(myproject_enable_coverage project_name)
  if(NOT
     CMAKE_BUILD_TYPE
     STREQUAL
     "Release")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      message(" -- ** Enabling coverage reporting**")
      target_compile_options(${project_name} INTERFACE --coverage -O0 -g)
      target_link_libraries(${project_name} INTERFACE --coverage)
      # second case covers clang-cl
    elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang" OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC))
      message(" -- ** Enabling coverage reporting**")
      target_compile_options(${project_name} INTERFACE -fprofile-instr-generate -fcoverage-mapping)
      target_link_libraries(${project_name} INTERFACE -fprofile-instr-generate -fcoverage-mapping)
    endif()
  else()
    message("We do not enable coverage on release builds.")
  endif()
endfunction()
