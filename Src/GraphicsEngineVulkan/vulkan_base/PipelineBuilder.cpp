module;
#include <cstdint>

#include <array>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "common/Utilities.hpp"

module kataglyphis.vulkan.pipeline_builder;

Kataglyphis::PipelineBuilder::PipelineBuilder() = default;

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setShaderStages(
  std::vector<vk::PipelineShaderStageCreateInfo> in_shader_stages)
{
    shader_stages = std::move(in_shader_stages);
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setVertexInput(
  std::vector<vk::VertexInputBindingDescription> bindings,
  std::vector<vk::VertexInputAttributeDescription> attributes)
{
    vertex_bindings = std::move(bindings);
    vertex_attributes = std::move(attributes);
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setCullMode(vk::CullModeFlags in_cull_mode)
{
    cull_mode = in_cull_mode;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setAlphaBlending(bool enable)
{
    alpha_blending = enable;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setColorAttachmentCount(uint32_t count)
{
    color_attachment_count = count;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setUseColorBlendState(bool in_use_color_blend_state)
{
    use_color_blend_state = in_use_color_blend_state;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setDepthTest(bool enable)
{
    depth_test = enable;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setDepthWrite(bool enable)
{
    depth_write = enable;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setDepthCompareOp(vk::CompareOp compare_op)
{
    depth_compare_op = compare_op;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setDepthClamp(bool enable)
{
    depth_clamp = enable;
    return *this;
}

Kataglyphis::PipelineBuilder &Kataglyphis::PipelineBuilder::setDynamicCullMode(bool enable)
{
    dynamic_cull_mode = enable;
    return *this;
}

vk::Pipeline Kataglyphis::PipelineBuilder::build(vk::Device device,
  vk::PipelineLayout pipeline_layout,
  vk::RenderPass render_pass,
  vk::PipelineCache pipeline_cache,
  uint32_t subpass,
  const char *error_message) const
{
    vk::PipelineVertexInputStateCreateInfo vertex_input_create_info;
    vertex_input_create_info.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_bindings.size());
    vertex_input_create_info.pVertexBindingDescriptions = vertex_bindings.empty() ? nullptr : vertex_bindings.data();
    vertex_input_create_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes.size());
    vertex_input_create_info.pVertexAttributeDescriptions =
      vertex_attributes.empty() ? nullptr : vertex_attributes.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    vk::PipelineViewportStateCreateInfo viewport_state_create_info;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports = nullptr;
    viewport_state_create_info.scissorCount = 1;
    viewport_state_create_info.pScissors = nullptr;

    // Viewport/scissor are always dynamic; cull mode is opt-in (setDynamicCullMode)
    // so only the pass that needs per-draw culling pays the per-draw vkCmdSetCullMode.
    std::vector<vk::DynamicState> dynamic_states = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    if (dynamic_cull_mode) { dynamic_states.push_back(vk::DynamicState::eCullMode); }
    vk::PipelineDynamicStateCreateInfo dynamic_state_create_info;
    dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state_create_info.pDynamicStates = dynamic_states.data();

    vk::PipelineRasterizationStateCreateInfo rasterizer_create_info;
    rasterizer_create_info.depthClampEnable = depth_clamp ? VK_TRUE : VK_FALSE;
    rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_create_info.polygonMode = vk::PolygonMode::eFill;
    rasterizer_create_info.lineWidth = 1.0F;
    rasterizer_create_info.cullMode = cull_mode;
    rasterizer_create_info.frontFace = front_face;
    rasterizer_create_info.depthBiasEnable = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisample_create_info;
    multisample_create_info.sampleShadingEnable = VK_FALSE;
    multisample_create_info.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_create_info;
    depth_stencil_create_info.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil_create_info.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil_create_info.depthCompareOp = depth_compare_op;
    depth_stencil_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_create_info.stencilTestEnable = VK_FALSE;

    vk::PipelineColorBlendAttachmentState color_state;
    color_state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                                 | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    if (alpha_blending) {
        color_state.blendEnable = VK_TRUE;
        color_state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        color_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_state.colorBlendOp = vk::BlendOp::eAdd;
        color_state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        color_state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        color_state.alphaBlendOp = vk::BlendOp::eAdd;
    } else {
        color_state.blendEnable = VK_FALSE;
    }

    std::vector<vk::PipelineColorBlendAttachmentState> const color_states(color_attachment_count, color_state);

    vk::PipelineColorBlendStateCreateInfo color_blending_create_info;
    color_blending_create_info.logicOpEnable = VK_FALSE;
    color_blending_create_info.attachmentCount = static_cast<uint32_t>(color_states.size());
    color_blending_create_info.pAttachments = color_states.data();

    vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info;
    graphics_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    graphics_pipeline_create_info.pStages = shader_stages.data();
    graphics_pipeline_create_info.pVertexInputState = &vertex_input_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &input_assembly;
    graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
    graphics_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    graphics_pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    graphics_pipeline_create_info.pMultisampleState = &multisample_create_info;
    graphics_pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
    graphics_pipeline_create_info.pColorBlendState =
      use_color_blend_state ? &color_blending_create_info : nullptr;
    graphics_pipeline_create_info.layout = pipeline_layout;
    graphics_pipeline_create_info.renderPass = render_pass;
    graphics_pipeline_create_info.subpass = subpass;
    graphics_pipeline_create_info.basePipelineHandle = nullptr;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    auto pipeline_result = device.createGraphicsPipelines(pipeline_cache, graphics_pipeline_create_info);
    if (pipeline_result.result != vk::Result::eSuccess) {
        ASSERT_VULKAN(pipeline_result.result, error_message)
    }
    return pipeline_result.value.front();
}
