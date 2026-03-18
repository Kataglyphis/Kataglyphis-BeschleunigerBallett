module;

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vulkan/vulkan.hpp>

#include "common/Utilities.hpp"

module kataglyphis.vulkan.debug;

namespace Kataglyphis::debug {
static vk::DebugUtilsMessengerEXT debugUtilsMessenger;

VKAPI_ATTR static VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
  void * /*pUserData*/)
{
    // Select prefix depending on flags passed to the callback
    std::string prefix;

    if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0) {
        prefix = "VERBOSE: ";
    } else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0) {
        prefix = "INFO: ";
    } else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        prefix = "WARNING: ";
    } else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        prefix = "ERROR: ";
    }

    // Display message to default output (console/logcat)
    std::stringstream debugMessage;
    debugMessage << prefix << "[" << pCallbackData->messageIdNumber << "][" << pCallbackData->pMessageIdName
                 << "] : " << pCallbackData->pMessage;

#ifdef __ANDROID__
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOGE("%s", debugMessage.str().c_str());
    } else {
        LOGD("%s", debugMessage.str().c_str());
    }
#else
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << debugMessage.str() << "\n";
    } else {
        std::cout << debugMessage.str() << "\n";
    }
    fflush(stdout);
#endif

    // The return value of this callback controls whether the Vulkan call that
    // caused the validation message will be aborted or not We return VK_FALSE as
    // we DON'T want Vulkan calls that cause a validation message to abort If you
    // instead want to have calls abort, pass in VK_TRUE and the function will
    // return VK_ERROR_VALIDATION_FAILED_EXT
    return VK_FALSE;
}

void setupDebugging(vk::Instance instance, vk::DebugReportFlagsEXT /*flags*/, vk::DebugReportCallbackEXT /*callBack*/)
{
    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI{};
    debugUtilsMessengerCI.messageSeverity =
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    debugUtilsMessengerCI.messageType =
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;
    debugUtilsMessengerCI.pfnUserCallback = debugUtilsMessengerCallback;

    vk::Result result;
    std::tie(result, debugUtilsMessenger) = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCI, nullptr);
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create debug messenger")
}

void freeDebugCallback(vk::Instance instance)
{
    if (debugUtilsMessenger) { instance.destroyDebugUtilsMessengerEXT(debugUtilsMessenger, nullptr); }
}
}// namespace Kataglyphis::debug
