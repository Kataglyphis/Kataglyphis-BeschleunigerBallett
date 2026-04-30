module;
#include <memory>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "common/FormatHelper.hpp"
#include "renderer/pushConstants/PushConstantPost.hpp"

#include "common/Utilities.hpp"
#include <imgui.h>
#include <imgui_impl_vulkan.h>

module kataglyphis.vulkan.post_stage;

import kataglyphis.vulkan.file;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.shader_helper;

Kataglyphis::VulkanRendererInternals::PostStage::PostStage() = default;

void Kataglyphis::VulkanRendererInternals::PostStage::init(std::shared_ptr<VulkanDevice>in_device,
  VulkanSwapChain *swapchain,
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    this->device = in_device;
    this->vulkanSwapChain = swapchain;

    createOffscreenTextureSampler();

    createPushConstantRange();
    createDepthbufferImage();
    createRenderpass();
    createGraphicsPipeline(descriptorSetLayouts);
    createFramebuffer();
}

void Kataglyphis::VulkanRendererInternals::PostStage::shaderHotReload(
  const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts)
{
    device->getLogicalDevice().destroyPipeline(graphics_pipeline);
    createGraphicsPipeline(descriptor_set_layouts);
}

void Kataglyphis::VulkanRendererInternals::PostStage::recordCommands(vk::CommandBuffer &commandBuffer,
  uint32_t image_index,
  const std::vector<vk::DescriptorSet> &descriptorSets,
  bool cloudsEnabled,
  bool shadowsEnabled,
  bool skyboxEnabled)
{
    vk::RenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.renderArea.offset = vk::Offset2D{ 0, 0 };
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    render_pass_begin_info.renderArea.extent = swap_chain_extent;

    std::array<vk::ClearValue, 2> clear_values;
    clear_values[0].color = vk::ClearColorValue{ 0.2F, 0.65F, 0.4F, 1.0F };
    clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0F, 0 };

    render_pass_begin_info.pClearValues = clear_values.data();
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());

    render_pass_begin_info.framebuffer = framebuffers[image_index];

    commandBuffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swap_chain_extent.width);
    viewport.height = static_cast<float>(swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer.setViewport(0, 1, &viewport);

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{ 0, 0 };
    scissor.extent = swap_chain_extent;
    commandBuffer.setScissor(0, 1, &scissor);

    auto aspectRatio = static_cast<float>(swap_chain_extent.width) / static_cast<float>(swap_chain_extent.height);
    PushConstantPost pc_post{};
    pc_post.aspect_ratio = aspectRatio;
    pc_post.clouds_enabled = cloudsEnabled ? 1u : 0u;
    pc_post.shadows_enabled = shadowsEnabled ? 1u : 0u;
    pc_post.skybox_enabled = skyboxEnabled ? 1u : 0u;
    commandBuffer.pushConstants(pipeline_layout,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
      0,
      sizeof(PushConstantPost),
      &pc_post);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptorSets, nullptr);
    commandBuffer.draw(3, 1, 0, 0);

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(commandBuffer));

    commandBuffer.endRenderPass();
}

void Kataglyphis::VulkanRendererInternals::PostStage::cleanUp()
{
    depthBufferImage->cleanUp();
    for (auto &framebuffer : framebuffers) { device->getLogicalDevice().destroyFramebuffer(framebuffer); }

    device->getLogicalDevice().destroySampler(offscreenTextureSampler);

    device->getLogicalDevice().destroyRenderPass(render_pass);
    device->getLogicalDevice().destroyPipeline(graphics_pipeline);
    device->getLogicalDevice().destroyPipelineLayout(pipeline_layout);
}

Kataglyphis::VulkanRendererInternals::PostStage::~PostStage() = default;

void Kataglyphis::VulkanRendererInternals::PostStage::destroyFramebuffers()
{
    for (auto &framebuffer : framebuffers) { device->getLogicalDevice().destroyFramebuffer(framebuffer); }
    framebuffers.clear();
}

void Kataglyphis::VulkanRendererInternals::PostStage::recreateFrameResources()
{
    depthBufferImage->cleanUp();

    createDepthbufferImage();
    createFramebuffer();
}

void Kataglyphis::VulkanRendererInternals::PostStage::createDepthbufferImage()
{
    depth_format = Kataglyphis::choose_supported_format(device->getPhysicalDevice(),
      { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint },
      vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    depthBufferImage = std::make_unique<Texture>();
    depthBufferImage->createImage(device,
      swap_chain_extent.width,
      swap_chain_extent.height,
      1,
      depth_format,
      vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eDepthStencilAttachment,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

    depthBufferImage->createImageView(device, depth_format, vk::ImageAspectFlagBits::eDepth, 1);
}

void Kataglyphis::VulkanRendererInternals::PostStage::createOffscreenTextureSampler()
{
    vk::PhysicalDeviceFeatures physical_device_features = device->getPhysicalDevice().getFeatures();

    vk::SamplerCreateInfo sampler_create_info;
    sampler_create_info.magFilter = vk::Filter::eLinear;
    sampler_create_info.minFilter = vk::Filter::eLinear;
    sampler_create_info.addressModeU = vk::SamplerAddressMode::eRepeat;
    sampler_create_info.addressModeV = vk::SamplerAddressMode::eRepeat;
    sampler_create_info.addressModeW = vk::SamplerAddressMode::eRepeat;
    sampler_create_info.borderColor = vk::BorderColor::eFloatOpaqueBlack;
    sampler_create_info.unnormalizedCoordinates = VK_FALSE;
    sampler_create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sampler_create_info.mipLodBias = 0.0F;
    sampler_create_info.minLod = 0.0F;
    sampler_create_info.maxLod = 0.0F;
    sampler_create_info.anisotropyEnable = physical_device_features.samplerAnisotropy;
    sampler_create_info.maxAnisotropy = (physical_device_features.samplerAnisotropy != 0u) ? 16.0F : 1.0F;

    vk::Result const result =
      device->getLogicalDevice().createSampler(&sampler_create_info, nullptr, &offscreenTextureSampler);
    ASSERT_VULKAN(result, "Failed to create a texture sampler!")
}

void Kataglyphis::VulkanRendererInternals::PostStage::createPushConstantRange()
{
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantPost);
}

void Kataglyphis::VulkanRendererInternals::PostStage::createRenderpass()
{
    vk::AttachmentDescription color_attachment;
    const vk::Format &swap_chain_image_format = vulkanSwapChain->getSwapChainFormat();
    color_attachment.format = swap_chain_image_format;
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

    color_attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
    color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentDescription depth_attachment;
    depth_attachment.format = Kataglyphis::choose_supported_format(device->getPhysicalDevice(),
      { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint },
      vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    depth_attachment.samples = vk::SampleCountFlagBits::e1;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference color_attachment_reference;
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depth_attachment_reference;
    depth_attachment_reference.attachment = 1;
    depth_attachment_reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_reference;
    subpass.pDepthStencilAttachment = &depth_attachment_reference;

    std::array<vk::SubpassDependency, 1> subpass_dependencies;

    subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    subpass_dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    subpass_dependencies[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    subpass_dependencies[0].dstSubpass = 0;
    subpass_dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    subpass_dependencies[0].dstAccessMask =
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
    subpass_dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    std::array<vk::AttachmentDescription, 2> render_pass_attachments = { color_attachment, depth_attachment };

    vk::RenderPassCreateInfo render_pass_create_info;
    render_pass_create_info.attachmentCount = static_cast<uint32_t>(render_pass_attachments.size());
    render_pass_create_info.pAttachments = render_pass_attachments.data();
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = static_cast<uint32_t>(subpass_dependencies.size());
    render_pass_create_info.pDependencies = subpass_dependencies.data();

    vk::Result const result =
      device->getLogicalDevice().createRenderPass(&render_pass_create_info, nullptr, &render_pass);
    ASSERT_VULKAN(result, "Failed to create render pass!")
}

void Kataglyphis::VulkanRendererInternals::PostStage::createGraphicsPipeline(
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    std::stringstream post_shader_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    post_shader_dir << cwd.string();
    post_shader_dir << RELATIVE_RESOURCE_PATH;
    post_shader_dir << "Shaders/post/";

    std::string const post_vert_shader = "post.vert";
    std::string const post_frag_shader = "post.frag";

    ShaderHelper shaderHelper;
    File vertexShaderFile(shaderHelper.getShaderSpvDir(post_shader_dir.str(), post_vert_shader));
    std::vector<char> const vertex_shader_code = vertexShaderFile.readCharSequence();
    File fragmentShaderFile(shaderHelper.getShaderSpvDir(post_shader_dir.str(), post_frag_shader));
    std::vector<char> const fragment_shader_code = fragmentShaderFile.readCharSequence();

    shaderHelper.compileShader(post_shader_dir.str(), post_vert_shader);
    shaderHelper.compileShader(post_shader_dir.str(), post_frag_shader);

    vk::ShaderModule vertex_shader_module = shaderHelper.createShaderModule(device, vertex_shader_code);
    vk::ShaderModule fragment_shader_module = shaderHelper.createShaderModule(device, fragment_shader_code);

    vk::PipelineShaderStageCreateInfo vertex_shader_create_info;
    vertex_shader_create_info.stage = vk::ShaderStageFlagBits::eVertex;
    vertex_shader_create_info.module = vertex_shader_module;
    vertex_shader_create_info.pName = "main";

    vk::PipelineShaderStageCreateInfo fragment_shader_create_info;
    fragment_shader_create_info.stage = vk::ShaderStageFlagBits::eFragment;
    fragment_shader_create_info.module = fragment_shader_module;
    fragment_shader_create_info.pName = "main";

    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = { vertex_shader_create_info,
        fragment_shader_create_info };

    vk::PipelineVertexInputStateCreateInfo vertex_input_create_info;
    vertex_input_create_info.vertexBindingDescriptionCount = 0;
    vertex_input_create_info.pVertexBindingDescriptions = nullptr;
    vertex_input_create_info.vertexAttributeDescriptionCount = 0;
    vertex_input_create_info.pVertexAttributeDescriptions = nullptr;

    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    vk::Viewport viewport;
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    viewport.width = static_cast<float>(swap_chain_extent.width);
    viewport.height = static_cast<float>(swap_chain_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;

    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D{ 0, 0 };
    scissor.extent = swap_chain_extent;

    vk::PipelineViewportStateCreateInfo viewport_state_create_info;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports = nullptr;
    viewport_state_create_info.scissorCount = 1;
    viewport_state_create_info.pScissors = nullptr;

    std::vector<vk::DynamicState> dynamic_states = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamic_state_create_info;
    dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state_create_info.pDynamicStates = dynamic_states.data();

    vk::PipelineRasterizationStateCreateInfo rasterizer_create_info;
    rasterizer_create_info.depthClampEnable = VK_FALSE;
    rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_create_info.polygonMode = vk::PolygonMode::eFill;
    rasterizer_create_info.lineWidth = 1.0F;
    rasterizer_create_info.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer_create_info.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer_create_info.depthBiasClamp = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisample_create_info;
    multisample_create_info.sampleShadingEnable = VK_FALSE;
    multisample_create_info.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState color_state;
    color_state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                                 | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    color_state.blendEnable = VK_TRUE;
    color_state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    color_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    color_state.colorBlendOp = vk::BlendOp::eAdd;
    color_state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    color_state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    color_state.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo color_blending_create_info;
    color_blending_create_info.logicOpEnable = VK_FALSE;
    color_blending_create_info.logicOp = vk::LogicOp::eClear;
    color_blending_create_info.attachmentCount = 1;
    color_blending_create_info.pAttachments = &color_state;
    for (int i = 0; i < 4; i++) { color_blending_create_info.blendConstants[0] = 0.F; }

    vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    vk::Result result =
      device->getLogicalDevice().createPipelineLayout(&pipeline_layout_create_info, nullptr, &pipeline_layout);
    ASSERT_VULKAN(result, "Failed to create pipeline layout!")

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_create_info;
    depth_stencil_create_info.depthTestEnable = VK_TRUE;
    depth_stencil_create_info.depthWriteEnable = VK_TRUE;
    depth_stencil_create_info.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depth_stencil_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_create_info.stencilTestEnable = VK_FALSE;

    vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info;
    graphics_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    graphics_pipeline_create_info.pStages = shader_stages.data();
    graphics_pipeline_create_info.pVertexInputState = &vertex_input_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &input_assembly;
    graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
    graphics_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    graphics_pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    graphics_pipeline_create_info.pMultisampleState = &multisample_create_info;
    graphics_pipeline_create_info.pColorBlendState = &color_blending_create_info;
    graphics_pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
    graphics_pipeline_create_info.layout = pipeline_layout;
    graphics_pipeline_create_info.renderPass = render_pass;
    graphics_pipeline_create_info.subpass = 0;

    graphics_pipeline_create_info.basePipelineHandle = nullptr;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    auto create_result = device->getLogicalDevice().createGraphicsPipeline(nullptr, graphics_pipeline_create_info);
    if (create_result.result == vk::Result::eSuccess) {
        graphics_pipeline = create_result.value;
    } else {
        ASSERT_VULKAN(create_result.result, "Failed to create a graphics pipeline!")
    }

    device->getLogicalDevice().destroyShaderModule(vertex_shader_module);
    device->getLogicalDevice().destroyShaderModule(fragment_shader_module);
}

void Kataglyphis::VulkanRendererInternals::PostStage::createFramebuffer()
{
    framebuffers.resize(vulkanSwapChain->getNumberSwapChainImages());

    for (size_t i = 0; i < vulkanSwapChain->getNumberSwapChainImages(); i++) {
        Texture &swap_chain_image = vulkanSwapChain->getSwapChainImage(static_cast<uint32_t>(i));

        std::array<vk::ImageView, 2> attachments = { swap_chain_image.getImageView(),
            depthBufferImage->getImageView() };

        vk::FramebufferCreateInfo frame_buffer_create_info;
        frame_buffer_create_info.renderPass = render_pass;
        frame_buffer_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        frame_buffer_create_info.pAttachments = attachments.data();
        const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        frame_buffer_create_info.width = swap_chain_extent.width;
        frame_buffer_create_info.height = swap_chain_extent.height;
        frame_buffer_create_info.layers = 1;

        vk::Result const result =
          device->getLogicalDevice().createFramebuffer(&frame_buffer_create_info, nullptr, &framebuffers[i]);
        ASSERT_VULKAN(result, "Failed to create framebuffer!")
    }
}