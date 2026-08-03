#pragma once

#include <vulkan/vulkan.hpp>

namespace Kataglyphis {

// Every compute pass in this engine built the same
// vk::PipelineShaderStageCreateInfo by hand: stage eCompute, pName "main".
// Slang always emits "main" as the entry-point symbol regardless of the
// Slang-side function name (AGENTS.md), so no call site has a reason to pass
// its own entry-point name here - a builder that took one would just be
// giving every future call site a way to get it wrong.
constexpr vk::PipelineShaderStageCreateInfo buildComputeShaderStageCreateInfo(vk::ShaderModule module)
{
    return vk::PipelineShaderStageCreateInfo{
        vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eCompute, module, "main"
    };
}

// flags is deliberately left at its default so a pass that needs one
// assigns it on the returned value rather than this helper growing a
// parameter, the same rule ViewportHelper.hpp and PipelineLayoutHelper.hpp
// both state.
//
// basePipelineHandle is passed explicitly as vk::Pipeline(nullptr) rather
// than left on vk::ComputePipelineCreateInfo's own default: that default
// argument invokes vk::Pipeline's non-constexpr default constructor, which
// would make every call site - including the static_assert this file's test
// suite builds against - not a constant expression.
constexpr vk::ComputePipelineCreateInfo buildComputePipelineCreateInfo(
  const vk::PipelineShaderStageCreateInfo &stage, vk::PipelineLayout layout)
{
    return vk::ComputePipelineCreateInfo{ vk::PipelineCreateFlags{}, stage, layout, vk::Pipeline(nullptr) };
}

}// namespace Kataglyphis
