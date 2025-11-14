# we depend on vulkan
find_package(Vulkan REQUIRED)
# configure vulkan version
set(VULKAN_VERSION_MAJOR 1)
set(VULKAN_VERSION_MINOR 3)

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

# we depend on OpenGL
find_package(OpenGL REQUIRED COMPONENTS OpenGL)
# configure OpenGL version
set(OPENGL_VERSION_MAJOR 4)
set(OPENGL_VERSION_MINOR 6)
set(OpenGL_GL_PREFERENCE GLVND)
