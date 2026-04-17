# we depend on vulkan
find_package(Vulkan REQUIRED)
# configure vulkan version
set(VULKAN_VERSION_MAJOR
    1
    CACHE STRING "Requested Vulkan API major version")
set(VULKAN_VERSION_MINOR
    4
    CACHE STRING "Requested Vulkan API minor version")
set(VULKAN_VERSION_PATCH
    0
    CACHE STRING "Requested Vulkan API patch version")

math(EXPR VULKAN_API_VERSION
     "(${VULKAN_VERSION_MAJOR} << 22) | (${VULKAN_VERSION_MINOR} << 12) | ${VULKAN_VERSION_PATCH}")

# Warn if Vulkan version is not compatible with Raspberry Pi
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  if(VULKAN_VERSION_MINOR GREATER 3)
    message(
      WARNING
        [[
            ⚠️ Vulkan ${VULKAN_VERSION_MAJOR}.${VULKAN_VERSION_MINOR} may not be supported on Raspberry Pi.
            Consider using Vulkan 1.3 or lower to ensure compatibility with Pi GPUs.
        ]])

  endif()
endif()

find_package(Threads REQUIRED)
