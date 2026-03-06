if(RUST_FEATURES)
  corrosion_import_crate(
    MANIFEST_PATH
    ../ExternalLib/Kataglyphis-RustProjectTemplate/Cargo.toml
    CRATE_TYPES
    staticlib
    CRATES
    kataglyphis_rustprojecttemplate)

  if(WIN32)
    target_link_libraries(
      kataglyphis_rustprojecttemplate
      INTERFACE kernel32
                ntdll
                userenv
                ws2_32
                dbghelp)
  endif()

  if(WIN32)
    # Ensure that Rust links MSVC debug runtime correctly in Debug mode
    # and properly sets iterator debug level to match CMake targets
    corrosion_set_env_vars(
      kataglyphis_rustprojecttemplate "CXXFLAGS=/EHsc $<$<CONFIG:Debug>:/MDd>$<$<NOT:$<CONFIG:Debug>>:/MD>"
      "CFLAGS=$<$<CONFIG:Debug>:/MDd>$<$<NOT:$<CONFIG:Debug>>:/MD>")
  else()
    if(NOT DEFINED GCC_TOOLCHAIN_PATH)
      set(GCC_TOOLCHAIN_PATH
          ""
          CACHE PATH "Optional GCC toolchain root")
    endif()

    set(RUST_CC "${CMAKE_C_COMPILER}")
    set(RUST_CXX "${CMAKE_CXX_COMPILER}")
    if(NOT RUST_CC)
      set(RUST_CC "cc")
    endif()
    if(NOT RUST_CXX)
      set(RUST_CXX "c++")
    endif()

    set(_gcc_toolchain_root "")
    if(GCC_TOOLCHAIN_PATH AND EXISTS "${GCC_TOOLCHAIN_PATH}")
      set(_gcc_toolchain_root "${GCC_TOOLCHAIN_PATH}")
    elseif(DEFINED MYPROJECT_GCC_TOOLCHAIN_PATH AND EXISTS "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
      set(_gcc_toolchain_root "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
    endif()

    set(RUST_CXXFLAGS "-fexceptions")
    if(_gcc_toolchain_root AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      set(RUST_CXXFLAGS "--gcc-toolchain=${_gcc_toolchain_root} -fexceptions")
    endif()
    if(DEFINED ENV{CXXFLAGS})
      set(RUST_CXXFLAGS "$ENV{CXXFLAGS} ${RUST_CXXFLAGS}")
    endif()
    corrosion_set_env_vars(
      kataglyphis_rustprojecttemplate
      CC=${RUST_CC}
      CXX=${RUST_CXX}
      "CXXFLAGS=${RUST_CXXFLAGS}")

    message(STATUS "Configured native corrosion: CC=${RUST_CC} CXX=${RUST_CXX} CXXFLAGS=${RUST_CXXFLAGS}")
  endif()

  corrosion_add_cxxbridge(
    kataglyphis_rustprojecttemplate_bridge
    CRATE
    kataglyphis_rustprojecttemplate
    FILES
    lib.rs)

endif()


add_subdirectory(GraphicsEngineOpenGL)
add_subdirectory(GraphicsEngineVulkan)
add_subdirectory(KomputePlayground)
