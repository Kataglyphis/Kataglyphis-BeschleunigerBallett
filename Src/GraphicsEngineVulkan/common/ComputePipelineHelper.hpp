#pragma once

#include <vulkan/vulkan.hpp>

#include "common/ShaderStageHelper.hpp"

namespace Kataglyphis {

// Thin delegation to the shared builder in ShaderStageHelper.hpp - kept as
// its own function (rather than inlining the eCompute call everywhere) so
// call sites and computePipelineHelperSuite.cpp don't need to know the
// compute stage bit.
constexpr vk::PipelineShaderStageCreateInfo buildComputeShaderStageCreateInfo(vk::ShaderModule module)
{
    return buildShaderStageCreateInfo(vk::ShaderStageFlagBits::eCompute, module);
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
