// Host-side mirror of the ray-tracing push-constant block. Its layout is
// pinned against the Slang redeclaration by BuildIntegrity.SharedStructOffsetsMatchTheCompiledSpirv.
#pragma once
#include "common/HostDeviceGlmAliases.hpp"
namespace Kataglyphis::VulkanRendererInternals {

struct PushConstantRaytracing
{
    vec4 clear_color;
};

}// namespace Kataglyphis::VulkanRendererInternals
