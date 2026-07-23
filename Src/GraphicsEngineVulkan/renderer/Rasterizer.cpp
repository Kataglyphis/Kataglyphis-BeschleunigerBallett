module;
#include <memory>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"

#include "common/FormatHelper.hpp"

#include "common/Utilities.hpp"

module kataglyphis.vulkan.rasterizer;

import kataglyphis.vulkan.file;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.pipeline_builder;

namespace {
auto hasStencilComponent(vk::Format format) -> bool
{
    return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
}
}// namespace

Kataglyphis::VulkanRendererInternals::Rasterizer::Rasterizer() = default;

void Kataglyphis::VulkanRendererInternals::Rasterizer::init(std::shared_ptr<VulkanDevice>in_device,
  VulkanSwapChain *swap_chain,
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts,
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
  const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts)
{
    device->getLogicalDevice().destroyPipeline(graphics_pipeline);
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
  const std::vector<vk::DescriptorSet> &descriptorSets,
  const std::optional<FrustumPlanes> &cameraFrustum)
{
    vk::RenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.renderArea.offset = vk::Offset2D{ 0, 0 };
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    render_pass_begin_info.renderArea.extent = swap_chain_extent;

    std::array<vk::ClearValue, 2> clear_values = {};
    clear_values[0].color = vk::ClearColorValue{ std::array<float, 4>{ 0.0F, 0.0F, 0.0F, 0.0F } };
    clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0F, 0 };

    render_pass_begin_info.pClearValues = clear_values.data();
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_begin_info.framebuffer = framebuffer[image_index];

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

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);
    // The set is identical for every mesh: bind once, not per draw.
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptorSets, nullptr);

    meshesDrawn = 0;
    meshesConsidered = 0;

    // object descriptions are flattened one-per-mesh across all models
    // (Scene::add_model), so objectIndex is the running FLAT mesh index, not the
    // model index. It advances for every mesh - culled ones included - to stay
    // aligned with that buffer. Identical to the old per-model push while each
    // Model holds one mesh.
    uint32_t flat_mesh_index = 0;
    for (uint32_t m = 0; m < scene->getModelCount(); m++) {
        pushConstant.model = scene->getModelMatrix(m);

        for (unsigned int k = 0; k < scene->getMeshCount(m); k++) {
            const uint32_t object_index = flat_mesh_index++;
            ++meshesConsidered;

            // Skip meshes provably outside the view. isVisible() is
            // conservative and treats unknown bounds as visible, so this can
            // only ever drop geometry the camera cannot see.
            if (cameraFrustum.has_value()
                && !isVisible(*cameraFrustum, transformAABB(pushConstant.model, scene->getMeshBounds(m, k)))) {
                continue;
            }

            pushConstant.objectIndex = object_index;
            commandBuffer.pushConstants(pipeline_layout,
              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
              0,
              sizeof(PushConstantRasterizer),
              &pushConstant);

            // glTF material.doubleSided: render both faces for this mesh, else
            // back-face cull. The pipeline declares eCullMode dynamic, so this
            // must be set for every draw (default eBack for OBJ / single-sided).
            commandBuffer.setCullMode(scene->isMeshDoubleSided(m, k) ? vk::CullModeFlagBits::eNone
                                                                     : vk::CullModeFlagBits::eBack);

            const vk::Buffer vertex_buffer = scene->getVertexBuffer(m, k);
            const vk::DeviceSize offset = 0;
            commandBuffer.bindVertexBuffers(0, 1, &vertex_buffer, &offset);

            commandBuffer.bindIndexBuffer(scene->getIndexBuffer(m, k), 0, vk::IndexType::eUint32);

            commandBuffer.drawIndexed(scene->getIndexCount(m, k), 1, 0, 0, 0);
            ++meshesDrawn;
        }
    }

    commandBuffer.endRenderPass();
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path).
    if (!device) { return; }

    spdlog::info("Rasterizer: Destroying pipeline handle: 0x{:x}", (uint64_t)(VkPipeline)graphics_pipeline);
    for (auto &framebuffer_handle : framebuffer) { device->getLogicalDevice().destroyFramebuffer(framebuffer_handle); }
    framebuffer.clear();

    for (const auto &texture : offscreenTextures) {
        if (texture) { texture->cleanUp(); }
    }
    offscreenTextures.clear();

    if (depthBufferImage) { depthBufferImage->cleanUp(); }
    depthBufferImage.reset();

    if (graphics_pipeline) {
        device->getLogicalDevice().destroyPipeline(graphics_pipeline);
        graphics_pipeline = nullptr;
    }
    if (pipeline_layout) {
        device->getLogicalDevice().destroyPipelineLayout(pipeline_layout);
        pipeline_layout = nullptr;
    }
    if (render_pass) {
        device->getLogicalDevice().destroyRenderPass(render_pass);
        render_pass = nullptr;
    }

    device.reset();
}

Kataglyphis::VulkanRendererInternals::Rasterizer::~Rasterizer() { cleanUp(); }

void Kataglyphis::VulkanRendererInternals::Rasterizer::destroyFramebuffers()
{
    for (auto &framebuffer_handle : framebuffer) { 
        spdlog::info("Rasterizer: Destroying framebuffer: 0x{:x}", (uint64_t)(VkFramebuffer)framebuffer_handle);
        device->getLogicalDevice().destroyFramebuffer(framebuffer_handle); 
    }
    framebuffer.clear();
}

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
    vk::AttachmentDescription color_attachment;
    // HDR: lighting now scales with the GUI light radiance (default 10),
    // so the lit scene exceeds 1.0 everywhere - a UNORM target clamps it
    // flat before post's tonemap ever runs (measured: everything at the
    // 186 ceiling). FP16 keeps the range for Reinhard.
    constexpr vk::Format offscreen_format = vk::Format::eR16G16B16A16Sfloat;
    color_attachment.format = offscreen_format;
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentDescription depth_attachment;
    depth_attachment.format = choose_supported_format(device->getPhysicalDevice(),
      { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint },
      vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    depth_attachment.samples = vk::SampleCountFlagBits::e1;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
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

    vk::RenderPassCreateInfo render_pass_create_info;
    render_pass_create_info.attachmentCount = static_cast<uint32_t>(render_pass_attachments.size());
    render_pass_create_info.pAttachments = render_pass_attachments.data();
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = static_cast<uint32_t>(subpass_dependencies.size());
    render_pass_create_info.pDependencies = subpass_dependencies.data();

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

        vk::FramebufferCreateInfo frame_buffer_create_info;
        frame_buffer_create_info.renderPass = render_pass;
        frame_buffer_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        frame_buffer_create_info.pAttachments = attachments.data();
        const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        frame_buffer_create_info.width = swap_chain_extent.width;
        frame_buffer_create_info.height = swap_chain_extent.height;
        frame_buffer_create_info.layers = 1;

        auto result = device->getLogicalDevice().createFramebuffer(frame_buffer_create_info);
        if (result.result == vk::Result::eSuccess) {
            framebuffer[i] = result.value;
            spdlog::info("Rasterizer: Created framebuffer[{}]: 0x{:x}", i, (uint64_t)(VkFramebuffer)framebuffer[i]);
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

    vk::CommandBuffer cmdBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(
      device->getLogicalDevice(), commandPool);

    for (uint32_t index = 0; index < vulkanSwapChain->getNumberSwapChainImages(); index++) {
        auto texture = std::make_unique<Texture>();
        const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        // HDR: lighting now scales with the GUI light radiance (default 10),
    // so the lit scene exceeds 1.0 everywhere - a UNORM target clamps it
    // flat before post's tonemap ever runs (measured: everything at the
    // 186 ceiling). FP16 keeps the range for Reinhard.
    constexpr vk::Format offscreen_format = vk::Format::eR16G16B16A16Sfloat;

        texture->createImage(device,
          swap_chain_extent.width,
          swap_chain_extent.height,
          1,
          offscreen_format,
          vk::ImageTiling::eOptimal,
          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage
            | vk::ImageUsageFlagBits::eTransferDst,
          vk::MemoryPropertyFlagBits::eDeviceLocal);

        texture->createImageView(device, offscreen_format, vk::ImageAspectFlagBits::eColor, 1);

        offscreenTextures[index] = std::move(texture);
    }

    vk::Format const depth_format = choose_supported_format(device->getPhysicalDevice(),
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

    vk::ImageAspectFlags depth_aspect_flags = vk::ImageAspectFlagBits::eDepth;
    if (hasStencilComponent(depth_format)) { depth_aspect_flags |= vk::ImageAspectFlagBits::eStencil; }

    depthBufferImage->createImageView(device, depth_format, depth_aspect_flags, 1);

    VulkanImage &vulkanImage = depthBufferImage->getVulkanImage();
    vulkanImage.transitionImageLayout(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      depth_aspect_flags,
      1);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), cmdBuffer);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createGraphicsPipeline(
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    std::stringstream rasterizer_shader_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    rasterizer_shader_dir << cwd.string();
    rasterizer_shader_dir << RELATIVE_RESOURCE_PATH;
    rasterizer_shader_dir << "Shaders/rasterizer/";

    ShaderHelper shaderHelper;
    shaderHelper.compileShader(rasterizer_shader_dir.str(), "shader.vert");
    shaderHelper.compileShader(rasterizer_shader_dir.str(), "shader.frag");

    File vertexFile(shaderHelper.getShaderSpvDir(rasterizer_shader_dir.str(), "shader.vert"));
    File fragmentFile(shaderHelper.getShaderSpvDir(rasterizer_shader_dir.str(), "shader.frag"));
    std::vector<char> const vertex_shader_code = vertexFile.readCharSequence();
    std::vector<char> const fragment_shader_code = fragmentFile.readCharSequence();

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

    vk::VertexInputBindingDescription binding_description;
    binding_description.binding = 0;
    binding_description.stride = sizeof(Vertex);
    binding_description.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 4> attribute_describtions = vertex::getVertexInputAttributeDesc();

    vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    auto layout_result = device->getLogicalDevice().createPipelineLayout(pipeline_layout_create_info);
    if (layout_result.result == vk::Result::eSuccess) {
        pipeline_layout = layout_result.value;
    } else {
        ASSERT_VULKAN(static_cast<VkResult>(layout_result.result), "Failed to create pipeline layout!")
    }

    PipelineBuilder pipeline_builder;
    graphics_pipeline =
      pipeline_builder.setShaderStages(shader_stages)
        .setVertexInput({ binding_description }, { attribute_describtions.begin(), attribute_describtions.end() })
        .setAlphaBlending(true)
        // Per-draw cull mode: doubleSided glTF meshes disable back-face culling
        // (set in the record loop). Every draw sets it explicitly below.
        .setDynamicCullMode(true)
        .setBasePipelineIndex(-1)
        .build(device->getLogicalDevice(), pipeline_layout, render_pass, device->getPipelineCache());
    spdlog::info("Rasterizer: Created pipeline handle: 0x{:x}", (uint64_t)(VkPipeline)graphics_pipeline);

    device->getLogicalDevice().destroyShaderModule(vertex_shader_module);
    device->getLogicalDevice().destroyShaderModule(fragment_shader_module);
}