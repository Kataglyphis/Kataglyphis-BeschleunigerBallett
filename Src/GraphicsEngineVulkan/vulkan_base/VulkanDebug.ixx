module;

#include <vulkan/vulkan.h>

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

void setupDebugging(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportCallbackEXT callBack);
void freeDebugCallback(VkInstance instance);

}// namespace Kataglyphis::debug