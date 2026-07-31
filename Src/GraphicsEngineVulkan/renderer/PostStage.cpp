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

import kataglyphis.vulkan.debug;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.pipeline_builder;

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

    {
        Kataglyphis::debug::ScopedCmdLabel const gui_label(commandBuffer, "gui", { 0.90F, 0.70F, 0.20F, 1.0F });
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(commandBuffer));
    }

    commandBuffer.endRenderPass();
}

void Kataglyphis::VulkanRendererInternals::PostStage::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path).
    if (!device) { return; }

    if (depthBufferImage) { depthBufferImage->cleanUp(); }
    depthBufferImage.reset();

    for (auto &framebuffer : framebuffers) { device->getLogicalDevice().destroyFramebuffer(framebuffer); }
    framebuffers.clear();

    if (offscreenTextureSampler) {
        device->getLogicalDevice().destroySampler(offscreenTextureSampler);
        offscreenTextureSampler = nullptr;
    }
    if (render_pass) {
        device->getLogicalDevice().destroyRenderPass(render_pass);
        render_pass = nullptr;
    }
    if (graphics_pipeline) {
        device->getLogicalDevice().destroyPipeline(graphics_pipeline);
        graphics_pipeline = nullptr;
    }
    if (pipeline_layout) {
        device->getLogicalDevice().destroyPipelineLayout(pipeline_layout);
        pipeline_layout = nullptr;
    }

    device.reset();
}

Kataglyphis::VulkanRendererInternals::PostStage::~PostStage() { cleanUp(); }

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
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build
    // time. Run from the repo root (per AGENTS.md).
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/post/";

    std::string const post_vert_spv = "post.vs_main.spv";
    std::string const post_frag_spv = "post.fs_main.spv";

    vk::ShaderModule vertex_shader_module = loadSpirvShaderModule(device, slang_spv_dir + post_vert_spv);
    vk::ShaderModule fragment_shader_module = loadSpirvShaderModule(device, slang_spv_dir + post_frag_spv);

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

    vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    vk::Result result =
      device->getLogicalDevice().createPipelineLayout(&pipeline_layout_create_info, nullptr, &pipeline_layout);
    ASSERT_VULKAN(result, "Failed to create pipeline layout!")

    PipelineBuilder pipeline_builder;
    graphics_pipeline = pipeline_builder.setShaderStages(shader_stages)
                          .setCullMode(vk::CullModeFlagBits::eNone)
                          .setAlphaBlending(true)
                          .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
                          .setBasePipelineIndex(-1)
                          .build(device->getLogicalDevice(), pipeline_layout, render_pass, device->getPipelineCache());

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