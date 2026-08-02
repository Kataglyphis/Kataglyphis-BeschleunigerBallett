module;
#include <memory>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include "common/FormatHelper.hpp"
#include "common/FramebufferHelper.hpp"
#include "common/RenderPassHelper.hpp"
#include "common/ViewportHelper.hpp"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.deferred_rasterizer;

import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.pipeline_builder;
import kataglyphis.vulkan.mesh_draw_recorder;

using namespace Kataglyphis::VulkanRendererInternals;

DeferredRasterizer::DeferredRasterizer() = default;

void DeferredRasterizer::init(std::shared_ptr<VulkanDevice>in_device,
  VulkanSwapChain *swap_chain,
  std::span<const vk::DescriptorSetLayout> descriptorSetLayouts)
{
    device = in_device;
    vulkanSwapChain = swap_chain;

    createTextures();
    createRenderPass();
    createPushConstantRange();
    createPipelines(descriptorSetLayouts);
    createFramebuffer();
}

void DeferredRasterizer::shaderHotReload(std::span<const vk::DescriptorSetLayout> descriptor_set_layouts)
{
    device->getLogicalDevice().destroyPipeline(geometryPipeline);
    device->getLogicalDevice().destroyPipelineLayout(geometryPipelineLayout);
    device->getLogicalDevice().destroyPipeline(lightingPipeline);
    device->getLogicalDevice().destroyPipelineLayout(lightingPipelineLayout);
    createPipelines(descriptor_set_layouts);
}

Kataglyphis::Texture &DeferredRasterizer::getOffscreenTexture(uint32_t index)
{
    return *offscreenTextures[index];
}

void DeferredRasterizer::setPushConstant(PushConstantRasterizer push_constant)
{
    pushConstant = push_constant;
}

void DeferredRasterizer::createTextures()
{
    uint32_t count = vulkanSwapChain->getNumberSwapChainImages();
    offscreenTextures.resize(count);
    gBufferNormals.resize(count);
    gBufferAlbedos.resize(count);
    gBufferMaterials.resize(count);

    const vk::Extent2D &extent = vulkanSwapChain->getSwapChainExtent();
    auto createAttachment = [&](std::vector<std::unique_ptr<Texture>>& textures, vk::Format format, vk::ImageUsageFlags usage) {
        for (uint32_t i = 0; i < count; i++) {
            auto tex = std::make_unique<Texture>();
            tex->createImage(device, extent.width, extent.height, 1, format, vk::ImageTiling::eOptimal, usage, vk::MemoryPropertyFlagBits::eDeviceLocal);
            tex->createImageView(device, format, vk::ImageAspectFlagBits::eColor, 1);
            textures[i] = std::move(tex);
        }
    };

    // Use specific formats for GBuffer
    createAttachment(offscreenTextures, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst);
    // No position attachment: the lighting pass reconstructs world position
    // from the DEPTH input attachment + the inverse view/projection - a full
    // rgba16f render target of bandwidth per frame for data the depth buffer
    // already encodes.
    createAttachment(gBufferNormals, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);
    createAttachment(gBufferAlbedos, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);
    createAttachment(gBufferMaterials, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);

    // Depth buffer. No transition is recorded here: createRenderPass declares
    // initialLayout = eUndefined for every attachment this function creates,
    // so the render pass itself performs the first transition.
    depthBufferImage = std::make_unique<Texture>();
    vk::Format depthFormat = Kataglyphis::chooseDepthFormat(device->getPhysicalDevice());
    depthBufferImage->createImage(device, extent.width, extent.height, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    // Input-attachment view: exactly one aspect, not Kataglyphis::depthStencilTransitionAspect. See its doc comment.
    depthBufferImage->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void DeferredRasterizer::createPushConstantRange()
{
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eAll;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantRasterizer);
}


void DeferredRasterizer::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path).
    if (!device) { return; }

    auto logicalDevice = device->getLogicalDevice();
    if (geometryPipeline) {
        logicalDevice.destroyPipeline(geometryPipeline);
        geometryPipeline = nullptr;
    }
    if (geometryPipelineLayout) {
        logicalDevice.destroyPipelineLayout(geometryPipelineLayout);
        geometryPipelineLayout = nullptr;
    }
    if (lightingPipeline) {
        logicalDevice.destroyPipeline(lightingPipeline);
        lightingPipeline = nullptr;
    }
    if (lightingPipelineLayout) {
        logicalDevice.destroyPipelineLayout(lightingPipelineLayout);
        lightingPipelineLayout = nullptr;
    }
    if (renderPass) {
        logicalDevice.destroyRenderPass(renderPass);
        renderPass = nullptr;
    }

    for (auto &fb : framebuffer) {
        logicalDevice.destroyFramebuffer(fb);
    }
    framebuffer.clear();

    for (auto& tex : offscreenTextures) { if (tex) tex->cleanUp(); }
    offscreenTextures.clear();
    for (auto& tex : gBufferNormals) { if (tex) tex->cleanUp(); }
    gBufferNormals.clear();
    for (auto& tex : gBufferAlbedos) { if (tex) tex->cleanUp(); }
    gBufferAlbedos.clear();
    for (auto& tex : gBufferMaterials) { if (tex) tex->cleanUp(); }
    gBufferMaterials.clear();
    if (depthBufferImage) { depthBufferImage->cleanUp(); }
    depthBufferImage.reset();

    device.reset();
}

DeferredRasterizer::~DeferredRasterizer() { cleanUp(); }

void Kataglyphis::VulkanRendererInternals::DeferredRasterizer::destroyFramebuffers()
{
    for (auto &fb : framebuffer) {
        device->getLogicalDevice().destroyFramebuffer(fb);
    }
    framebuffer.clear();
}

void Kataglyphis::VulkanRendererInternals::DeferredRasterizer::recreateFrameResources()
{
    for (auto& tex : offscreenTextures) { if (tex) tex->cleanUp(); }
    offscreenTextures.clear();
    for (auto& tex : gBufferNormals) { if (tex) tex->cleanUp(); }
    gBufferNormals.clear();
    for (auto& tex : gBufferAlbedos) { if (tex) tex->cleanUp(); }
    gBufferAlbedos.clear();
    for (auto& tex : gBufferMaterials) { if (tex) tex->cleanUp(); }
    gBufferMaterials.clear();
    if (depthBufferImage) { depthBufferImage->cleanUp(); }
    depthBufferImage.reset();

    createTextures();
    createFramebuffer();
}

void DeferredRasterizer::createRenderPass()
{
    // Attachments
    // 0: Final Color (Offscreen)
    // 1: Normal
    // 2: Albedo
    // 3: Material
    // 4: Depth
    // No position attachment - see the comment at createTextures's :87-90.

    // HDR final target - matches the forward offscreen (see Rasterizer.cpp).
    vk::Format finalFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format normalFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format albedoFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format materialFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format depthFormat = Kataglyphis::chooseDepthFormat(device->getPhysicalDevice());

    // All five use the engine-wide attachment defaults (clear on load, store,
    // start from eUndefined) - see common/RenderPassHelper.hpp. The local
    // createAttachmentDesc lambda this replaces was one of five hand-written
    // copies of the same field list.
    std::array<vk::AttachmentDescription, 5> attachments = {
        buildAttachmentDescription(finalFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 0: Final Output
        buildAttachmentDescription(normalFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 1: Normal
        buildAttachmentDescription(albedoFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 2: Albedo
        buildAttachmentDescription(materialFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 3: Material
        buildAttachmentDescription(depthFormat, vk::ImageLayout::eDepthStencilAttachmentOptimal) // 4: Depth
    };

    // Subpass 0: Geometry Pass
    std::array<vk::AttachmentReference, 3> geometryColorRefs = {
        vk::AttachmentReference{1, vk::ImageLayout::eColorAttachmentOptimal}, // Normal
        vk::AttachmentReference{2, vk::ImageLayout::eColorAttachmentOptimal}, // Albedo
        vk::AttachmentReference{3, vk::ImageLayout::eColorAttachmentOptimal}  // Material
    };
    vk::AttachmentReference geometryDepthRef{4, vk::ImageLayout::eDepthStencilAttachmentOptimal};

    vk::SubpassDescription geometrySubpass;
    geometrySubpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    geometrySubpass.colorAttachmentCount = static_cast<uint32_t>(geometryColorRefs.size());
    geometrySubpass.pColorAttachments = geometryColorRefs.data();
    geometrySubpass.pDepthStencilAttachment = &geometryDepthRef;

    // Subpass 1: Lighting Pass
    vk::AttachmentReference lightingColorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
    
    std::array<vk::AttachmentReference, 4> lightingInputRefs = {
        vk::AttachmentReference{1, vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::AttachmentReference{2, vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::AttachmentReference{3, vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::AttachmentReference{4, vk::ImageLayout::eShaderReadOnlyOptimal}
    };

    vk::SubpassDescription lightingSubpass;
    lightingSubpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    lightingSubpass.colorAttachmentCount = 1;
    lightingSubpass.pColorAttachments = &lightingColorRef;
    lightingSubpass.inputAttachmentCount = static_cast<uint32_t>(lightingInputRefs.size());
    lightingSubpass.pInputAttachments = lightingInputRefs.data();

    // Dependencies
    std::array<vk::SubpassDependency, 3> dependencies;

    // External -> Geometry Subpass
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
    dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
    dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    // Geometry Subpass -> Lighting Subpass
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = 1;
    dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead;
    dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    // Lighting Subpass -> External
    dependencies[2].srcSubpass = 1;
    dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[2].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependencies[2].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
    dependencies[2].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
    dependencies[2].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
    dependencies[2].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    std::array<vk::SubpassDescription, 2> subpasses = {geometrySubpass, lightingSubpass};

    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    auto result = device->getLogicalDevice().createRenderPass(renderPassInfo);
    ASSERT_VULKAN(VkResult(result.result), "Failed to create deferred render pass!")
    renderPass = result.value;
}

void DeferredRasterizer::createPipelines(std::span<const vk::DescriptorSetLayout> descriptorSetLayouts)
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/deferred/";

    ShaderStagePair geomStages{ device, slang_spv_dir + "deferred.geometry_vs_main.spv",
        slang_spv_dir + "deferred.geometry_fs_main.spv" };

    vk::VertexInputBindingDescription bindingDescription;
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = vk::VertexInputRate::eVertex;
    
    std::array<vk::VertexInputAttributeDescription, 4> attributeDescriptions = vertex::getVertexInputAttributeDesc();

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push_constant_range;

    vk::ResultValue<vk::PipelineLayout> geometry_pipeline_layout_result =
      device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    ASSERT_VULKAN(geometry_pipeline_layout_result.result, "Failed to create deferred geometry pipeline layout!")
    geometryPipelineLayout = geometry_pipeline_layout_result.value;

    PipelineBuilder geometryPipelineBuilder;
    geometryPipeline =
      geometryPipelineBuilder.setShaderStages({ geomStages.stages().begin(), geomStages.stages().end() })
        .setVertexInput({ bindingDescription }, { attributeDescriptions.begin(), attributeDescriptions.end() })
        .setColorAttachmentCount(3)
        // Per-draw cull mode so doubleSided glTF meshes disable back-face culling
        // in the G-buffer pass too (set in the record loop below).
        .setDynamicCullMode(true)
        .build(device->getLogicalDevice(), geometryPipelineLayout, renderPass, device->getPipelineCache(), 0);

    // Lighting Pipeline (Slang-emitted SPIR-V)
    ShaderStagePair lightStages{ device, slang_spv_dir + "deferred.lighting_vs_main.spv",
        slang_spv_dir + "deferred.lighting_fs_main.spv" };

    vk::PipelineLayoutCreateInfo lightPipelineLayoutInfo{};
    lightPipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    lightPipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    // No push constants for lighting pass usually, or same if needed

    vk::ResultValue<vk::PipelineLayout> lighting_pipeline_layout_result =
      device->getLogicalDevice().createPipelineLayout(lightPipelineLayoutInfo);
    ASSERT_VULKAN(lighting_pipeline_layout_result.result, "Failed to create deferred lighting pipeline layout!")
    lightingPipelineLayout = lighting_pipeline_layout_result.value;

    PipelineBuilder lightingPipelineBuilder;
    lightingPipeline = lightingPipelineBuilder.setShaderStages({ lightStages.stages().begin(), lightStages.stages().end() })
                         // Vertex-less fullscreen triangle (gl_VertexIndex in lighting.vert):
                         // no vertex buffer is ever bound, so declare an empty vertex input.
                         .setVertexInput({}, {})
                         .setCullMode(vk::CullModeFlagBits::eNone)
                         .setDepthTest(false)
                         .setDepthWrite(false)
                         .build(device->getLogicalDevice(), lightingPipelineLayout, renderPass, device->getPipelineCache(), 1);
}

void DeferredRasterizer::createFramebuffer()
{
    std::array<vk::ImageView, 5> attachments;
    const vk::Extent2D &extent = vulkanSwapChain->getSwapChainExtent();

    framebuffer.resize(vulkanSwapChain->getNumberSwapChainImages());
    for (uint32_t i = 0; i < vulkanSwapChain->getNumberSwapChainImages(); i++) {
        attachments[0] = offscreenTextures[i]->getImageView();
        attachments[1] = gBufferNormals[i]->getImageView();
        attachments[2] = gBufferAlbedos[i]->getImageView();
        attachments[3] = gBufferMaterials[i]->getImageView();
        attachments[4] = depthBufferImage->getImageView();

        const vk::FramebufferCreateInfo framebufferInfo =
          Kataglyphis::buildFramebufferCreateInfo(renderPass, attachments, extent);

        auto result = device->getLogicalDevice().createFramebuffer(framebufferInfo);
        ASSERT_VULKAN(VkResult(result.result), "Failed to create deferred framebuffer!");
        framebuffer[i] = result.value;
    }
}


void DeferredRasterizer::recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, Kataglyphis::Scene *scene, std::span<const vk::DescriptorSet> descriptorSets, const std::optional<FrustumPlanes> &cameraFrustum)
{
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();

    std::array<vk::ClearValue, 5> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[1].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[2].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[3].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[4].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    const vk::RenderPassBeginInfo renderPassInfo = Kataglyphis::buildRenderPassBeginInfo(
      renderPass, framebuffer[image_index], swap_chain_extent, clearValues);

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    setFullExtentViewportAndScissor(commandBuffer, swap_chain_extent);

    // Subpass 0: Geometry
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, geometryPipeline);

    // Bind global descriptor set (set 0). It is identical for every mesh: bind
    // once, not per draw (matches Rasterizer::recordCommands).
    commandBuffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, geometryPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

    // objectIndex is the flat mesh index into the per-mesh object-description
    // buffer; see Rasterizer::recordCommands. Advances for every mesh (culled
    // included); no-op vs the old per-model push while a Model holds one mesh.
    const MeshDrawStats draw_stats = recordSceneMeshDraws(
      commandBuffer, geometryPipelineLayout, vk::ShaderStageFlagBits::eAll, scene, cameraFrustum, pushConstant);
    meshesDrawn = draw_stats.drawn;
    meshesConsidered = draw_stats.considered;

    // Transition to Subpass 1: Lighting
    commandBuffer.nextSubpass(vk::SubpassContents::eInline);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, lightingPipeline);
    
    // Bind lighting pass specific descriptor set (set 1) alongside global (set 0) if needed
    // Assuming descriptorSets[0] is global, descriptorSets[1] is GBuffer inputs
    if (descriptorSets.size() > 1) {
        commandBuffer.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, lightingPipelineLayout, 1, 1, &descriptorSets[1], 0, nullptr);
    }
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, lightingPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

    commandBuffer.draw(3, 1, 0, 0); // Fullscreen triangle

    commandBuffer.endRenderPass();
}
