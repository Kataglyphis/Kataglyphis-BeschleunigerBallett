module;

#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.debug;

export namespace Kataglyphis::debug {
extern int validationLayerCount;
extern const char *validationLayerNames[];

VKAPI_ATTR VkBool32 VKAPI_CALL messageCallback(VkDebugReportFlagsEXT flags,
  VkDebugReportObjectTypeEXT objType,
  uint64_t srcObject,
  size_t location,
  int32_t msgCode,
  const char *pLayerPrefix,
  const char *pMsg,
  void *pUserData);

void setupDebugging(vk::Instance instance, vk::DebugReportFlagsEXT flags, vk::DebugReportCallbackEXT callBack);
void freeDebugCallback(vk::Instance instance);

}// namespace Kataglyphis::debug