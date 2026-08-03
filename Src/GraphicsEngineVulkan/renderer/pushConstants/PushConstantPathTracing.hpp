// Host-side mirror of the path-tracing push-constant block. Its layout is
// pinned against the Slang redeclaration by BuildIntegrity.SharedStructOffsetsMatchTheCompiledSpirv.
#pragma once
#include "common/HostDeviceGlmAliases.hpp"
namespace Kataglyphis::VulkanRendererInternals {

struct PushConstantPathTracing
{
    vec4 clearColor;
    uint width;
    uint height;
    // Frames accumulated since the last history reset (camera move / resize).
    // 0 means "discard history"; also folded into the RNG seed so every frame
    // draws different samples.
    uint frame_index;
    // GUI-driven quality: samples per pixel per frame and the bounce cap.
    uint samples_per_pixel;
    uint max_bounces;
};

}// namespace Kataglyphis::VulkanRendererInternals
