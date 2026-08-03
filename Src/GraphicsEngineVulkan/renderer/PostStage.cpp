module;
#include <memory>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "common/FormatHelper.hpp"
#include "common/FramebufferHelper.hpp"
#include "common/PipelineLayoutHelper.hpp"
#include "common/RenderPassHelper.hpp"
#include "common/ViewportHelper.hpp"
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
import kataglyphis.vulkan.sampler_builder;

Kataglyphis::VulkanRendererInternals::PostStage::PostStage() = default;

void Kataglyphis::VulkanRendererInternals::PostStage::init(std::shared_ptr<VulkanDevice>in_device,
  VulkanSwapChain *swapchain,
  std::span<const vk::DescriptorSetLayout> descriptorSetLayouts)
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
  std::span<const vk::DescriptorSetLayout> descriptor_set_layouts)
{
    Kataglyphis::destroyPipelineAndLayout(device->getLogicalDevice(), graphics_pipeline, pipeline_layout);
    createGraphicsPipeline(descriptor_set_layouts);
}

void Kataglyphis::VulkanRendererInternals::PostStage::recordCommands(vk::CommandBuffer &commandBuffer,
  uint32_t image_index,
  std::span<const vk::DescriptorSet> descriptorSets,
  bool cloudsEnabled)
{
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();

    std::array<vk::ClearValue, 2> clear_values;
    clear_values[0].color = vk::ClearColorValue{ 0.2F, 0.65F, 0.4F, 1.0F };
    clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0F, 0 };

    const vk::RenderPassBeginInfo render_pass_begin_info = Kataglyphis::buildRenderPassBeginInfo(
      render_pass, framebuffers[image_index], swap_chain_extent, clear_values);

    commandBuffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

    setFullExtentViewportAndScissor(commandBuffer, swap_chain_extent);

    PushConstantPost pc_post{};
    pc_post.clouds_enabled = cloudsEnabled ? 1u : 0u;
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

    destroyFramebuffers();

    if (offscreenTextureSampler) {
        device->getLogicalDevice().destroySampler(offscreenTextureSampler);
        offscreenTextureSampler = nullptr;
    }
    Kataglyphis::destroyRenderPass(device->getLogicalDevice(), render_pass);
    Kataglyphis::destroyPipelineAndLayout(device->getLogicalDevice(), graphics_pipeline, pipeline_layout);

    device.reset();
}

Kataglyphis::VulkanRendererInternals::PostStage::~PostStage() { cleanUp(); }

void Kataglyphis::VulkanRendererInternals::PostStage::destroyFramebuffers()
{
    Kataglyphis::destroyFramebuffers(device->getLogicalDevice(), framebuffers);
}

// Rebuilds framebuffers but deliberately does not destroy the previous ones -
// VulkanRenderer::recreateSwapChain() must call destroyFramebuffers() before
// this, while the swapchain images they reference still exist.
void Kataglyphis::VulkanRendererInternals::PostStage::recreateFrameResources()
{
    depthBufferImage->cleanUp();

    createDepthbufferImage();
    createFramebuffer();
}

void Kataglyphis::VulkanRendererInternals::PostStage::createDepthbufferImage()
{
    depth_format = Kataglyphis::chooseDepthFormat(device->getPhysicalDevice());

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

    // Depth-only attachment view, no layout transition: exactly one aspect,
    // not Kataglyphis::depthStencilTransitionAspect. See its doc comment.
    depthBufferImage->createImageView(device, depth_format, vk::ImageAspectFlagBits::eDepth, 1);
}

void Kataglyphis::VulkanRendererInternals::PostStage::createOffscreenTextureSampler()
{
    vk::PhysicalDeviceFeatures physical_device_features = device->getPhysicalDevice().getFeatures();

    vk::SamplerCreateInfo sampler_create_info = buildSamplerCreateInfo(vk::Filter::eLinear,
      vk::SamplerAddressMode::eRepeat,
      0.0F,
      physical_device_features.samplerAnisotropy,
      (physical_device_features.samplerAnisotropy != 0u) ? 16.0F : 1.0F,
      vk::BorderColor::eFloatOpaqueBlack);

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
    // The only pass that overrides all three attachment defaults: the skybox
    // pass already rendered into this swapchain image, so its contents are
    // LOADED (not cleared) out of eColorAttachmentOptimal (not eUndefined) and
    // handed to the presentation engine.
    const vk::AttachmentDescription color_attachment = buildAttachmentDescription(
      vulkanSwapChain->getSwapChainFormat(),
      vk::ImageLayout::ePresentSrcKHR,
      vk::AttachmentLoadOp::eLoad,
      vk::AttachmentStoreOp::eStore,
      vk::ImageLayout::eColorAttachmentOptimal);

    // depth_format was already resolved by createDepthbufferImage(), which
    // init() always runs first - reuse it rather than querying again, so the
    // attachment and the image it is paired with cannot diverge.
    const vk::AttachmentDescription depth_attachment = buildAttachmentDescription(
      depth_format,
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::AttachmentLoadOp::eClear,
      vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::AttachmentReference color_attachment_reference;
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depth_attachment_reference;
    depth_attachment_reference.attachment = 1;
    depth_attachment_reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    const vk::SubpassDescription subpass = buildSubpassDescription(
      std::span<const vk::AttachmentReference>(&color_attachment_reference, 1), &depth_attachment_reference);

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
    const std::array<vk::SubpassDescription, 1> subpasses = { subpass };

    vk::RenderPassCreateInfo const render_pass_create_info =
      Kataglyphis::buildRenderPassCreateInfo(render_pass_attachments, subpasses, subpass_dependencies);

    auto result = device->getLogicalDevice().createRenderPass(render_pass_create_info);
    ASSERT_VULKAN(static_cast<VkResult>(result.result), "Failed to create render pass!")
    render_pass = result.value;
}

void Kataglyphis::VulkanRendererInternals::PostStage::createGraphicsPipeline(
  std::span<const vk::DescriptorSetLayout> descriptorSetLayouts)
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build
    // time. Run from the repo root (per AGENTS.md).
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/post/";

    std::string const post_vert_spv = "post.vs_main.spv";
    std::string const post_frag_spv = "post.fs_main.spv";

    ShaderStagePair stages{ device, slang_spv_dir + post_vert_spv, slang_spv_dir + post_frag_spv };

    const std::array<vk::PushConstantRange, 1> push_constant_ranges = { push_constant_range };
    vk::PipelineLayoutCreateInfo pipeline_layout_create_info =
      buildPipelineLayoutCreateInfo(descriptorSetLayouts, push_constant_ranges);

    vk::ResultValue<vk::PipelineLayout> pipeline_layout_result =
      device->getLogicalDevice().createPipelineLayout(pipeline_layout_create_info);
    ASSERT_VULKAN(pipeline_layout_result.result, "Failed to create pipeline layout!")
    pipeline_layout = pipeline_layout_result.value;

    PipelineBuilder pipeline_builder;
    graphics_pipeline = pipeline_builder.setShaderStages({ stages.stages().begin(), stages.stages().end() })
                          .setCullMode(vk::CullModeFlagBits::eNone)
                          .setAlphaBlending(true)
                          .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
                          .build(device->getLogicalDevice(), pipeline_layout, render_pass, device->getPipelineCache());
}

void Kataglyphis::VulkanRendererInternals::PostStage::createFramebuffer()
{
    framebuffers.resize(vulkanSwapChain->getNumberSwapChainImages());

    for (size_t i = 0; i < vulkanSwapChain->getNumberSwapChainImages(); i++) {
        Texture &swap_chain_image = vulkanSwapChain->getSwapChainImage(static_cast<uint32_t>(i));

        std::array<vk::ImageView, 2> attachments = { swap_chain_image.getImageView(),
            depthBufferImage->getImageView() };

        const vk::FramebufferCreateInfo frame_buffer_create_info = Kataglyphis::buildFramebufferCreateInfo(
          render_pass, attachments, vulkanSwapChain->getSwapChainExtent());

        auto result = device->getLogicalDevice().createFramebuffer(frame_buffer_create_info);
        ASSERT_VULKAN(static_cast<VkResult>(result.result), "Failed to create framebuffer!")
        framebuffers[i] = result.value;
    }
}