#pragma once

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every rasterizer and raytracing pass in this engine built the same
// vk::PipelineShaderStageCreateInfo by hand: stage <whatever>, pName "main".
// Slang always emits "main" as the entry-point symbol regardless of the
// Slang-side function name (AGENTS.md), so no call site has a reason to pass
// its own entry-point name here - a builder that took one would just be
// giving every future call site a way to get it wrong. See
// ComputePipelineHelper.hpp's buildComputeShaderStageCreateInfo for the
// compute-only sibling of this builder.
constexpr vk::PipelineShaderStageCreateInfo buildShaderStageCreateInfo(vk::ShaderStageFlagBits stage,
  vk::ShaderModule module)
{ return vk::PipelineShaderStageCreateInfo{ vk::PipelineShaderStageCreateFlags{}, stage, module, "main" }; }

// Raytracing's raygen and miss shader groups are both "general" groups: a
// single shader index with the other three slots left unused. Kept as a
// dedicated builder (rather than folding into buildTrianglesHitGroup) so
// each call site states its intent without unused-slot arguments.
constexpr vk::RayTracingShaderGroupCreateInfoKHR buildGeneralShaderGroup(uint32_t generalShader)
{
    return vk::RayTracingShaderGroupCreateInfoKHR{ vk::RayTracingShaderGroupTypeKHR::eGeneral,
        generalShader,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR };
}

// Raytracing's closest-hit shader group is a triangles-hit group: no general
// shader, a closest-hit index, and any-hit/intersection left unused (the
// engine has no any-hit or procedural-intersection shaders today).
constexpr vk::RayTracingShaderGroupCreateInfoKHR buildTrianglesHitGroup(uint32_t closestHitShader)
{
    return vk::RayTracingShaderGroupCreateInfoKHR{ vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
        VK_SHADER_UNUSED_KHR,
        closestHitShader,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR };
}

}// namespace Kataglyphis
