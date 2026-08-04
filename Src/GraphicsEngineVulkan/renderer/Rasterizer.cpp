module;
#include <memory>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <sstream>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"

#include "common/FormatHelper.hpp"
#include "common/FramebufferHelper.hpp"
#include "common/PipelineLayoutHelper.hpp"
#include "common/RenderPassHelper.hpp"
#include "common/ViewportHelper.hpp"

#include "common/Utilities.hpp"

module kataglyphis.vulkan.rasterizer;

import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.pipeline_builder;
import kataglyphis.vulkan.mesh_draw_recorder;
import kataglyphis.vulkan.depth_attachment;

Kataglyphis::VulkanRendererInternals::Rasterizer::Rasterizer() = default;

void Kataglyphis::VulkanRendererInternals::Rasterizer::init(std::shared_ptr<VulkanDevice>in_device,
  VulkanSwapChain *swap_chain,
  std::span<const vk::DescriptorSetLayout> descriptorSetLayouts,
  vk::CommandPool &commandPool)
{
    this->device = in_device;
    this->vulkanSwapChain = swap_chain;

    createTextures(commandPool);
    createRenderPass();
    createPushConstantRange();
    createGraphicsPipeline(descriptorSetLayouts);
    createFramebuffer();
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::shaderHotReload(
  std::span<const vk::DescriptorSetLayout> descriptor_set_layouts)
{
    Kataglyphis::destroyPipelineAndLayout(device->getLogicalDevice(), graphics_pipeline, pipeline_layout);
    createGraphicsPipeline(descriptor_set_layouts);
}

auto Kataglyphis::VulkanRendererInternals::Rasterizer::getOffscreenTexture(uint32_t index) -> Kataglyphis::Texture &
{
    return *offscreenTextures[index];
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::setPushConstant(PushConstantRasterizer push_constant)
{
    this->pushConstant = push_constant;
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::recordCommands(vk::CommandBuffer &commandBuffer,
  uint32_t image_index,
  Scene *scene,
  std::span<const vk::DescriptorSet> descriptorSets,
  const std::optional<FrustumPlanes> &cameraFrustum)
{
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();

    std::array<vk::ClearValue, 2> clear_values = {};
    clear_values[0].color = vk::ClearColorValue{ std::array<float, 4>{ 0.0F, 0.0F, 0.0F, 0.0F } };
    clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0F, 0 };

    const vk::RenderPassBeginInfo render_pass_begin_info = Kataglyphis::buildRenderPassBeginInfo(
      render_pass, framebuffer[image_index], swap_chain_extent, clear_values);

    commandBuffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

    setFullExtentViewportAndScissor(commandBuffer, swap_chain_extent);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);
    // The set is identical for every mesh: bind once, not per draw.
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptorSets, nullptr);

    const MeshDrawStats draw_stats = recordSceneMeshDraws(commandBuffer,
      pipeline_layout,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
      scene,
      cameraFrustum,
      pushConstant);
    meshesDrawn = draw_stats.drawn;
    meshesConsidered = draw_stats.considered;

    commandBuffer.endRenderPass();
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path).
    if (!device) { return; }

    destroyFramebuffers();

    for (const auto &texture : offscreenTextures) {
        if (texture) { texture->cleanUp(); }
    }
    offscreenTextures.clear();

    if (depthBufferImage) { depthBufferImage->cleanUp(); }
    depthBufferImage.reset();

    Kataglyphis::destroyPipelineAndLayout(device->getLogicalDevice(), graphics_pipeline, pipeline_layout);
    Kataglyphis::destroyRenderPass(device->getLogicalDevice(), render_pass);

    device.reset();
}

Kataglyphis::VulkanRendererInternals::Rasterizer::~Rasterizer() { cleanUp(); }

void Kataglyphis::VulkanRendererInternals::Rasterizer::destroyFramebuffers()
{
    Kataglyphis::destroyFramebuffers(device->getLogicalDevice(), framebuffer);
}

// Rebuilds framebuffers but deliberately does not destroy the previous ones -
// VulkanRenderer::recreateSwapChain() must call destroyFramebuffers() before
// this, while the swapchain images they reference still exist.
void Kataglyphis::VulkanRendererInternals::Rasterizer::recreateFrameResources(vk::CommandPool commandPool)
{
    for (const auto &texture : offscreenTextures) { texture->cleanUp(); }
    offscreenTextures.clear();

    depthBufferImage->cleanUp();

    createTextures(commandPool);
    createFramebuffer();
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createRenderPass()
{
    // The offscreen colour target is sampled by the post stage afterwards.
    const vk::AttachmentDescription color_attachment =
      buildAttachmentDescription(OFFSCREEN_FORMAT, vk::ImageLayout::eShaderReadOnlyOptimal);

    // Depth is never read after the pass, so its writeback is eDontCare.
    // depth_format was already resolved by createTextures(), which init()
    // always runs first - reuse it rather than querying again, so the
    // attachment and the image it is paired with cannot diverge.
    const vk::AttachmentDescription depth_attachment =
      buildAttachmentDescription(depth_format,
        vk::ImageLayout::eDepthStencilAttachmentOptimal,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eDontCare);

    vk::AttachmentReference color_attachment_reference;
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depth_attachment_reference;
    depth_attachment_reference.attachment = 1;
    depth_attachment_reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    const vk::SubpassDescription subpass = buildSubpassDescription(
      std::span<const vk::AttachmentReference>(&color_attachment_reference, 1), &depth_attachment_reference);

    std::array<vk::SubpassDependency, 1> subpass_dependencies{};

    subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    // Depth is cleared via loadOp after an initial layout transition; the
    // dependency must cover EARLY/LATE_FRAGMENT_TESTS + depth writes or the
    // clear races the transition (SYNC-HAZARD-WRITE-AFTER-WRITE).
    subpass_dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput
                                           | vk::PipelineStageFlagBits::eEarlyFragmentTests
                                           | vk::PipelineStageFlagBits::eLateFragmentTests;
    // The single depth buffer is shared across frames in flight: the
    // previous frame's storeOp write must be made available before this
    // frame's clear (cross-submission WAW otherwise).
    subpass_dependencies[0].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    subpass_dependencies[0].dstSubpass = 0;
    subpass_dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput
                                           | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    subpass_dependencies[0].dstAccessMask =
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    subpass_dependencies[0].dependencyFlags = vk::DependencyFlags{};

    std::array<vk::AttachmentDescription, 2> render_pass_attachments = { color_attachment, depth_attachment };
    const std::array<vk::SubpassDescription, 1> subpasses = { subpass };

    vk::RenderPassCreateInfo const render_pass_create_info =
      Kataglyphis::buildRenderPassCreateInfo(render_pass_attachments, subpasses, subpass_dependencies);

    auto result = device->getLogicalDevice().createRenderPass(render_pass_create_info);
    if (result.result == vk::Result::eSuccess) {
        render_pass = result.value;
    } else {
        ASSERT_VULKAN(static_cast<VkResult>(result.result), "Failed to create render pass!")
    }
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createFramebuffer()
{
    framebuffer.resize(vulkanSwapChain->getNumberSwapChainImages());

    for (size_t i = 0; i < framebuffer.size(); i++) {
        std::array<vk::ImageView, 2> attachments = { offscreenTextures[i]->getImageView(),
            depthBufferImage->getImageView() };

        const vk::FramebufferCreateInfo frame_buffer_create_info = Kataglyphis::buildFramebufferCreateInfo(
          render_pass, attachments, vulkanSwapChain->getSwapChainExtent());

        auto result = device->getLogicalDevice().createFramebuffer(frame_buffer_create_info);
        if (result.result == vk::Result::eSuccess) {
            framebuffer[i] = result.value;
        } else {
            ASSERT_VULKAN(static_cast<VkResult>(result.result), "Failed to create framebuffer!")
        }
    }
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createPushConstantRange()
{
    // Fragment too: the frag shader indexes object_description with it.
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantRasterizer);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createTextures(vk::CommandPool &commandPool)
{
    offscreenTextures.resize(vulkanSwapChain->getNumberSwapChainImages());

    for (uint32_t index = 0; index < vulkanSwapChain->getNumberSwapChainImages(); index++) {
        auto texture = std::make_unique<Texture>();
        const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();

        texture->createImage(device,
          swap_chain_extent.width,
          swap_chain_extent.height,
          1,
          OFFSCREEN_FORMAT,
          vk::ImageTiling::eOptimal,
          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage
            | vk::ImageUsageFlagBits::eTransferDst,
          vk::MemoryPropertyFlagBits::eDeviceLocal);

        texture->createImageView(device, OFFSCREEN_FORMAT, vk::ImageAspectFlagBits::eColor, 1);

        offscreenTextures[index] = std::move(texture);
    }

    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    depthBufferImage = std::make_unique<Texture>();

    vk::ImageAspectFlags depth_aspect_flags =
      depthStencilTransitionAspect(chooseDepthFormat(device->getPhysicalDevice()));

    depth_format = createDepthAttachment(*depthBufferImage, device, swap_chain_extent, {}, depth_aspect_flags);

    // This overload allocates, submits and fence-waits its own command buffer -
    // there is no outer command buffer batching this transition.
    VulkanImage &vulkanImage = depthBufferImage->getVulkanImage();
    vulkanImage.transitionImageLayout(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      depth_aspect_flags,
      1);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createGraphicsPipeline(
  std::span<const vk::DescriptorSetLayout> descriptorSetLayouts)
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md) — a relative path works from there.
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/rasterizer/";

    ShaderStagePair stages{ device, slang_spv_dir + "rasterizer.vs_main.spv", slang_spv_dir + "rasterizer.fs_main.spv" };

    vk::VertexInputBindingDescription binding_description;
    binding_description.binding = 0;
    binding_description.stride = sizeof(Vertex);
    binding_description.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 4> attribute_describtions = vertex::getVertexInputAttributeDesc();

    const std::array<vk::PushConstantRange, 1> push_constant_ranges = { push_constant_range };
    vk::PipelineLayoutCreateInfo pipeline_layout_create_info =
      buildPipelineLayoutCreateInfo(descriptorSetLayouts, push_constant_ranges);

    auto layout_result = device->getLogicalDevice().createPipelineLayout(pipeline_layout_create_info);
    ASSERT_VULKAN(static_cast<VkResult>(layout_result.result), "Failed to create pipeline layout!")
    pipeline_layout = layout_result.value;

    PipelineBuilder pipeline_builder;
    graphics_pipeline =
      pipeline_builder.setShaderStages({ stages.stages().begin(), stages.stages().end() })
        .setVertexInput({ binding_description }, { attribute_describtions.begin(), attribute_describtions.end() })
        .setAlphaBlending(true)
        // Per-draw cull mode: doubleSided glTF meshes disable back-face culling
        // (set in the record loop). Every draw sets it explicitly below.
        .setDynamicCullMode(true)
        .build(device->getLogicalDevice(), pipeline_layout, render_pass, device->getPipelineCache());
}