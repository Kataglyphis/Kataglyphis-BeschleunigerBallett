include(${CMAKE_CURRENT_LIST_DIR}/../../../shared/cmake/filters/SetExternalLibsFilters.common.cmake)
kataglyphis_set_imgui_filter(
  "${CMAKE_SOURCE_DIR}/ExternalLib"
  "${CMAKE_SOURCE_DIR}/ExternalLib/IMGUI/backends/imgui_impl_vulkan.h"
  "${CMAKE_SOURCE_DIR}/ExternalLib/IMGUI/backends/imgui_impl_vulkan.cpp")
