if(RUST_FEATURES)
	corrosion_import_crate(
		MANIFEST_PATH ../ExternalLib/Kataglyphis-RustProjectTemplate/Cargo.toml
		CRATE_TYPES
		staticlib
		CRATES
		kataglyphis_rustprojecttemplate)
	if(WIN32)
		corrosion_set_env_vars(kataglyphis_rustprojecttemplate CXXFLAGS=/EHsc)
	else()
		# Native-only corrosion/toolchain setup (no cross-building)
		# Optionally override with -DGCC_TOOLCHAIN_PATH=/path/to/gcc when configuring.
		# This is optional and should not be required for standard GCC installs.
		if(NOT DEFINED GCC_TOOLCHAIN_PATH)
			set(GCC_TOOLCHAIN_PATH
					""
					CACHE PATH "Optional GCC toolchain root")
		endif()

		# Determine which compiler Rust/C++ bridge should use.
		# Keep this aligned with the selected CMake compiler.
		set(RUST_CC "${CMAKE_C_COMPILER}")
		set(RUST_CXX "${CMAKE_CXX_COMPILER}")
		if(NOT RUST_CC)
			set(RUST_CC "cc")
		endif()
		if(NOT RUST_CXX)
			set(RUST_CXX "c++")
		endif()

		# For Clang, optionally point it at a GCC toolchain for libstdc++/headers.
		# Prefer explicit GCC_TOOLCHAIN_PATH, otherwise fall back to MYPROJECT_GCC_TOOLCHAIN_PATH (top-level).
		set(_gcc_toolchain_root "")
		if(GCC_TOOLCHAIN_PATH AND EXISTS "${GCC_TOOLCHAIN_PATH}")
			set(_gcc_toolchain_root "${GCC_TOOLCHAIN_PATH}")
		elseif(DEFINED MYPROJECT_GCC_TOOLCHAIN_PATH AND EXISTS "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
			set(_gcc_toolchain_root "${MYPROJECT_GCC_TOOLCHAIN_PATH}")
		endif()

		set(RUST_CXXFLAGS "-fexceptions")
		if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND _gcc_toolchain_root)
			set(RUST_CXXFLAGS "--gcc-toolchain=${_gcc_toolchain_root} -fexceptions")
		endif()
		if(DEFINED ENV{CXXFLAGS})
			set(RUST_CXXFLAGS "$ENV{CXXFLAGS} ${RUST_CXXFLAGS}")
		endif()

		# If Clang is active and no explicit GCC toolchain was provided, prefer system GCC/G++ for cxx bridge.
		# This avoids missing C++ standard library headers (e.g. <algorithm>) in container/toolchain setups.
		if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT _gcc_toolchain_root)
			find_program(SYSTEM_GCC NAMES gcc)
			find_program(SYSTEM_GXX NAMES g++)
			if(SYSTEM_GCC AND SYSTEM_GXX)
				set(RUST_CC "${SYSTEM_GCC}")
				set(RUST_CXX "${SYSTEM_GXX}")
				set(RUST_CXXFLAGS "-fexceptions")
				message(STATUS "Using system GCC/G++ for Corrosion bridge: CC=${RUST_CC} CXX=${RUST_CXX}")
			endif()
		endif()

		# detect host arch for logging (no cross-compile behavior)
		string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _hostproc)
		if(_hostproc MATCHES "^(x86_64|amd64)$")
			set(HOST_ARCH "x86_64")
		elseif(_hostproc MATCHES "^(aarch64|arm64|armv8)$")
			set(HOST_ARCH "aarch64")
		elseif(_hostproc MATCHES "^(riscv64)$")
			set(HOST_ARCH "riscv64")
		else()
			set(HOST_ARCH "${_hostproc}")
		endif()

		message(STATUS "Native-only build. HOST_ARCH=${HOST_ARCH}")

		if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
			 AND _gcc_toolchain_root
			 AND EXISTS "${_gcc_toolchain_root}")
			# Try to find reasonable native compiler binary names
			if(HOST_ARCH STREQUAL "x86_64")
				find_program(GCC_TOOLCHAIN_GCC NAMES "${_gcc_toolchain_root}/bin/x86_64-pc-linux-gnu-gcc"
																						 "${_gcc_toolchain_root}/bin/gcc")
				find_program(GCC_TOOLCHAIN_GXX NAMES "${_gcc_toolchain_root}/bin/x86_64-pc-linux-gnu-g++"
																						 "${_gcc_toolchain_root}/bin/g++")
			elseif(HOST_ARCH STREQUAL "aarch64")
				find_program(GCC_TOOLCHAIN_GCC NAMES "${_gcc_toolchain_root}/bin/aarch64-linux-gnu-gcc"
																						 "${_gcc_toolchain_root}/bin/gcc")
				find_program(GCC_TOOLCHAIN_GXX NAMES "${_gcc_toolchain_root}/bin/aarch64-linux-gnu-g++"
																						 "${_gcc_toolchain_root}/bin/g++")
			elseif(HOST_ARCH STREQUAL "riscv64")
				find_program(GCC_TOOLCHAIN_GCC NAMES "${_gcc_toolchain_root}/bin/riscv64-unknown-linux-gnu-gcc"
																						 "${_gcc_toolchain_root}/bin/gcc")
				find_program(GCC_TOOLCHAIN_GXX NAMES "${_gcc_toolchain_root}/bin/riscv64-unknown-linux-gnu-g++"
																						 "${_gcc_toolchain_root}/bin/g++")
			else()
				find_program(GCC_TOOLCHAIN_GCC NAMES "${_gcc_toolchain_root}/bin/gcc")
				find_program(GCC_TOOLCHAIN_GXX NAMES "${_gcc_toolchain_root}/bin/g++")
			endif()

			if(GCC_TOOLCHAIN_GCC)
				set(RUST_CC "${GCC_TOOLCHAIN_GCC}")
			else()
				message(WARNING "Toolchain gcc not found in ${_gcc_toolchain_root}; falling back to system compiler")
			endif()
			if(GCC_TOOLCHAIN_GXX)
				set(RUST_CXX "${GCC_TOOLCHAIN_GXX}")
			else()
				message(WARNING "Toolchain g++ not found in ${_gcc_toolchain_root}; falling back to system compiler")
			endif()
		endif()

		# Always set deterministic env for corrosion on non-Windows hosts.
		corrosion_set_env_vars(
			kataglyphis_rustprojecttemplate
			CC=${RUST_CC}
			CXX=${RUST_CXX}
			"CXXFLAGS=${RUST_CXXFLAGS}")

		message(STATUS "Configured native corrosion: CC=${RUST_CC} CXX=${RUST_CXX} CXXFLAGS=${RUST_CXXFLAGS}")
	endif()

endif()

add_subdirectory(GraphicsEngineOpenGL)
add_subdirectory(GraphicsEngineVulkan)
add_subdirectory(KomputePlayground)
