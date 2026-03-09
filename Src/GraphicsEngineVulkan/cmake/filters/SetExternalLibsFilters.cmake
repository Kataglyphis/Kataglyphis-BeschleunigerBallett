set(KATAGLYPHIS_IMGUI_EXTRA_BACKENDS ${EXTERNAL_LIB_SRC_DIR}IMGUI/backends/imgui_impl_vulkan.h
                                     ${EXTERNAL_LIB_SRC_DIR}IMGUI/backends/imgui_impl_vulkan.cpp)
include(${CMAKE_CURRENT_LIST_DIR}/../../../shared/cmake/filters/SetExternalLibsFilters.common.cmake)
unset(KATAGLYPHIS_IMGUI_EXTRA_BACKENDS)
