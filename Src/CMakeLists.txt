function(_myproject_resolve_rust_gcc_toolchain_root out_var)
  if(NOT DEFINED GCC_TOOLCHAIN_PATH)
    set(GCC_TOOLCHAIN_PATH
        ""
        CACHE PATH "Optional GCC toolchain root")
  endif()

  set(_gcc_toolchain_root "")
  if(GCC_TOOLCHAIN_PATH AND EXISTS "${GCC_TOOLCHAIN_PATH}")
    set(_gcc_toolchain_root "${GCC_TOOLCHAIN_PATH}")
  elseif(DEFINED MYPROJECT_GCC_TOOLCHAIN_PATH AND EXISTS "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
    set(_gcc_toolchain_root "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
  endif()

  set(${out_var} "${_gcc_toolchain_root}" PARENT_SCOPE)
endfunction()

function(_myproject_configure_windows_rust_crate rust_target)
  target_link_libraries(
    ${rust_target}
    INTERFACE kernel32
              ntdll
              userenv
              ws2_32
              dbghelp)

  set(_RUST_WINDOWS_DEBUG_CXXFLAGS "/MDd /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2")
  set(_RUST_WINDOWS_DEBUG_CFLAGS "/MDd /D_DEBUG")

  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
     AND MSVC
     AND myproject_ENABLE_SANITIZER_ADDRESS)
    set(_RUST_WINDOWS_DEBUG_CXXFLAGS "/MD /D_ITERATOR_DEBUG_LEVEL=0")
    set(_RUST_WINDOWS_DEBUG_CFLAGS "/MD")
  endif()

  set(_RUST_WINDOWS_CXXFLAGS
      "/EHsc $<$<CONFIG:Debug>:${_RUST_WINDOWS_DEBUG_CXXFLAGS}>$<$<NOT:$<CONFIG:Debug>>:/MD /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0>")
  set(_RUST_WINDOWS_CFLAGS "$<$<CONFIG:Debug>:${_RUST_WINDOWS_DEBUG_CFLAGS}>$<$<NOT:$<CONFIG:Debug>>:/MD /DNDEBUG>")

  corrosion_set_env_vars(${rust_target} "CXXFLAGS=${_RUST_WINDOWS_CXXFLAGS}" "CFLAGS=${_RUST_WINDOWS_CFLAGS}")
endfunction()

function(_myproject_configure_native_rust_crate rust_target)
  _myproject_resolve_rust_gcc_toolchain_root(_gcc_toolchain_root)

  set(RUST_CC "${CMAKE_C_COMPILER}")
  set(RUST_CXX "${CMAKE_CXX_COMPILER}")
  if(NOT RUST_CC)
    set(RUST_CC "cc")
  endif()
  if(NOT RUST_CXX)
    set(RUST_CXX "c++")
  endif()

  set(RUST_CXXFLAGS "-fexceptions")
  if(_gcc_toolchain_root AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(RUST_CXXFLAGS "--gcc-toolchain=${_gcc_toolchain_root} -fexceptions")
  endif()
  if(DEFINED ENV{CXXFLAGS})
    set(RUST_CXXFLAGS "$ENV{CXXFLAGS} ${RUST_CXXFLAGS}")
  endif()

  corrosion_set_env_vars(
    ${rust_target}
    CC=${RUST_CC}
    CXX=${RUST_CXX}
    "CXXFLAGS=${RUST_CXXFLAGS}")

  message(STATUS "Configured native corrosion: CC=${RUST_CC} CXX=${RUST_CXX} CXXFLAGS=${RUST_CXXFLAGS}")
endfunction()

if(RUST_FEATURES)
  corrosion_import_crate(
    MANIFEST_PATH
    ../ExternalLib/Kataglyphis-RustProjectTemplate/Cargo.toml
    CRATE_TYPES
    staticlib
    CRATES
    kataglyphis_rustprojecttemplate)

  if(WIN32)
    _myproject_configure_windows_rust_crate(kataglyphis_rustprojecttemplate)
  else()
    _myproject_configure_native_rust_crate(kataglyphis_rustprojecttemplate)
  endif()

  # Keep the CXX bridge generated unconditionally so bridge headers are always available.
  corrosion_add_cxxbridge(
    kataglyphis_rustprojecttemplate_bridge
    CRATE
    kataglyphis_rustprojecttemplate
    FILES
    native_only.rs)

endif()

add_subdirectory(GraphicsEngineVulkan)
# The Kompute playground is a standalone compute-API demo, not part of the
# engine: kompute's own headers throw, so it CANNOT link the project's
# no-exceptions options (/EHs-, -fno-exceptions) - it deliberately builds
# with its own flags. Default OFF so ordinary builds ship no
# exceptions-enabled binary and skip the kompute dependency entirely.
if(KATAGLYPHIS_BUILD_KOMPUTE_PLAYGROUND)
  add_subdirectory(KomputePlayground)
endif()
