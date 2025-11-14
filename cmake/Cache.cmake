cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

function(myproject_enable_cache)
  # 1. Declare the cache option (default = empty → “no cache”)
  set(COMPILER_CACHE
      ""
      CACHE STRING "Compiler cache to be used (ccache or sccache; leave empty to disable)")

  # 2. Determine allowed backends
  if(MSVC AND WIN32)
    set(_allowed_values sccache)
  else()
    set(_allowed_values ccache sccache)
  endif()

  # 3. Attach combo‐box metadata
  set_property(CACHE "COMPILER_CACHE" PROPERTY STRINGS ${_allowed_values})

  # 4. Validate only if the user provided a value
  if(NOT
     "${COMPILER_CACHE}"
     STREQUAL
     "")
    list(
      FIND
      _allowed_values
      "${COMPILER_CACHE}"
      _idx) # returns -1 if not found :contentReference[oaicite:4]{index=4}
    if(_idx EQUAL -1)
      message(FATAL_ERROR "Invalid value for compiler_cache: '${COMPILER_CACHE}'.\n"
                          "Supported values are: ${_allowed_values}")
    endif()
  endif()

  # 5. Only integrate cache if requested
  if(NOT
     "${COMPILER_CACHE}"
     STREQUAL
     "")
    find_program(
      CACHE_BINARY
      NAMES "${COMPILER_CACHE}"
      DOC "Path to the compiler cache executable"
    )# creates CACHE_BINARY or <VAR>-NOTFOUND :contentReference[oaicite:5]{index=5}

    if(CACHE_BINARY
       AND NOT
           CACHE_BINARY
           STREQUAL
           "${PATH}-NOTFOUND")
      message(STATUS "${COMPILER_CACHE} found at ${CACHE_BINARY}. Enabling compiler cache.")

      # 6. Hook into C/C++ compiler launches
      set(CMAKE_C_COMPILER_LAUNCHER
          "${CACHE_BINARY}"
          CACHE STRING "C compiler cache launcher")
      set(CMAKE_CXX_COMPILER_LAUNCHER
          "${CACHE_BINARY}"
          CACHE STRING "CXX compiler cache launcher")

      # 7. MSVC: Embedded PDBs for cache consistency
      if(MSVC)
        if(POLICY CMP0141)
          cmake_policy(SET CMP0141 NEW)
          set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
              Embedded
              CACHE STRING "MSVC debug info format (use /Z7)")
          message(STATUS "Configured MSVC to embed PDB info (/Z7) for cache consistency.")
        else()
          string(
            REPLACE "/Zi"
                    "/Z7"
                    CMAKE_C_FLAGS_DEBUG
                    "${CMAKE_C_FLAGS_DEBUG}")
          string(
            REPLACE "/Zi"
                    "/Z7"
                    CMAKE_CXX_FLAGS_DEBUG
                    "${CMAKE_CXX_FLAGS_DEBUG}")
          message(STATUS "Replaced /Zi with /Z7 in debug flags for cache consistency.")
        endif()
      endif()

    else()
      message(WARNING "${COMPILER_CACHE} was requested but not found. Skipping cache integration.")
    endif()
  endif()
endfunction()
