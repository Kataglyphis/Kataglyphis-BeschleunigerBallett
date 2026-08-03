// Host-side mirror of the post push-constant block. Its layout is pinned
// against the Slang redeclaration by BuildIntegrity.SharedStructOffsetsMatchTheCompiledSpirv.
#pragma once
#include "common/HostDeviceGlmAliases.hpp"
namespace Kataglyphis::VulkanRendererInternals {

struct PushConstantPost
{
    uint clouds_enabled;
};

}// namespace Kataglyphis::VulkanRendererInternals
