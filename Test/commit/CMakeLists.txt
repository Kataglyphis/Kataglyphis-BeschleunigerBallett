if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
	message(STATUS "Skipping commit tests: build type is '${CMAKE_BUILD_TYPE}', requires Debug.")
	return()
endif()

add_subdirectory(VulkanEngine)
add_subdirectory(OpenGLEngine)
