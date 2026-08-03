// Host-side mirror of the global UBO. Its layout is pinned against the
// Slang redeclaration by BuildIntegrity.SharedStructOffsetsMatchTheCompiledSpirv.
#pragma once
#include "common/HostDeviceGlmAliases.hpp"
namespace Kataglyphis::VulkanRendererInternals {

// this will also be an input to our shaders !!
// which render stage doesn't need view,projection ?
struct GlobalUBO
{
    mat4 projection;
    mat4 view;
    // Precomputed on the CPU once per frame: the clouds compute shader
    // needs these per pixel, where inverse() is ruinously expensive.
    mat4 inv_projection;
    mat4 inv_view;
};
}// namespace Kataglyphis::VulkanRendererInternals