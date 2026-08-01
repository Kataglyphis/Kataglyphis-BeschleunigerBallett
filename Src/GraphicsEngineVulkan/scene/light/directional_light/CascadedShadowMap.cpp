module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <memory>
#include <filesystem>
#include <span>
#include <sstream>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"
#include "renderer/SceneUBO.hpp"

module kataglyphis.vulkan.cascaded_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.pipeline_builder;

namespace Kataglyphis {


void CascadedShadowMap::init(std::shared_ptr<VulkanDevice>in_device, uint32_t width, uint32_t height, uint32_t num_cascades,
  vk::DescriptorSetLayout sharedRenderDescriptorSetLayout)
{
    this->device = in_device;
    this->shadowWidth = width;
    this->shadowHeight = height;
    this->numCascades = num_cascades;
    this->sharedRenderDescriptorSetLayout = sharedRenderDescriptorSetLayout;
    
    cascadeData.resize(numCascades);

    vk::Format depthFormat = Kataglyphis::chooseDepthFormat(device->getPhysicalDevice());
    // Remember it: createRenderPass() and createFramebuffers() must attach the
    // very same format, and hard-coding eD32Sfloat there silently coupled them
    // to the head of the preference list above.
    depth_format = depthFormat;

    // Create 2D Texture Array for Cascades
    shadowMapArray = std::make_unique<Texture>();
    shadowMapArray->createImage(device, shadowWidth, shadowHeight, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, numCascades);

    // Create a view for the entire array (used in descriptor set for reading later)
    shadowMapArray->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray, numCascades);
    shadowMapArray->createTextureSampler(device, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge);

    createRenderPass();
    createFramebuffers();
}

// clampCascadeCount / makeShadowPush / computeCascadeData (and their private
// frustumCornersWorldSpace helper) moved to CascadedShadowMapMath.cpp - a
// separate implementation unit of this same module, so a consumer that only
// needs the pure-math free functions (Test/perf/perfSuite.cpp) never links
// this TU's Scene/Device/Texture dependencies.

void CascadedShadowMap::updateCascades(const glm::mat4 &cameraView,
  float cameraFov,
  float aspect,
  float nearPlane,
  float farPlane,
  const glm::vec3 &lightDir,
  float shadowDistance,
  float splitLambda)
{
    cascadeData = computeCascadeData(numCascades,
      cameraView,
      cameraFov,
      aspect,
      nearPlane,
      farPlane,
      lightDir,
      shadowDistance,
      splitLambda,
      shadowWidth);

    // Push the freshly computed matrices into the UBO the shadow geometry
    // shader renders with. Without this the buffer keeps whatever was in
    // cascadeData when createGraphicsPipeline() ran at init - i.e. default
    // constructed matrices - so the shadow map was rendered from a garbage
    // viewpoint while the lighting shader sampled it with the correct ones.
    if (void *mapped = lightMatricesBuffer.getMappedData(); mapped != nullptr) {
        std::vector<glm::mat4> lightMatrices(cascadeData.size());
        for (size_t i = 0; i < cascadeData.size(); i++) { lightMatrices[i] = cascadeData[i].viewProjMatrix; }
        std::memcpy(mapped, lightMatrices.data(), lightMatrices.size() * sizeof(glm::mat4));
    }
}

void CascadedShadowMap::createRenderPass()
{
    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = depth_format;
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<vk::SubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
    dependencies[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
    dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

    // Single-pass layered rendering: one render pass broadcasts every draw
    // to all cascade views (gl_ViewIndex selects the light matrix in the
    // vertex shader). Replaces one-pass-per-cascade plus a pass-through
    // geometry shader; features11.multiview is requested at device creation.
    const uint32_t view_mask = (1U << numCascades) - 1U;
    vk::RenderPassMultiviewCreateInfo multiviewInfo{};
    multiviewInfo.subpassCount = 1;
    multiviewInfo.pViewMasks = &view_mask;
    multiviewInfo.correlationMaskCount = 1;
    multiviewInfo.pCorrelationMasks = &view_mask;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.pNext = &multiviewInfo;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    auto result = device->getLogicalDevice().createRenderPass(renderPassInfo);
    ASSERT_VULKAN(VkResult(result.result), "Failed to create cascaded shadow map render pass!");
    renderPass = result.value;
}

void CascadedShadowMap::createFramebuffers()
{
    // One framebuffer over the FULL cascade array: multiview requires
    // framebuffer layers == 1 with the attachment view spanning every
    // rendered layer. The per-layer views and per-cascade framebuffers are
    // gone.
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = shadowMapArray->getImage();
    viewInfo.viewType = vk::ImageViewType::e2DArray;
    viewInfo.format = depth_format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = numCascades;

    auto viewResult = device->getLogicalDevice().createImageView(viewInfo);
    ASSERT_VULKAN(VkResult(viewResult.result), "Failed to create shadow map array view!");
    shadowMapArrayView = viewResult.value;

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &shadowMapArrayView;
    framebufferInfo.width = shadowWidth;
    framebufferInfo.height = shadowHeight;
    framebufferInfo.layers = 1;

    auto fbResult = device->getLogicalDevice().createFramebuffer(framebufferInfo);
    ASSERT_VULKAN(VkResult(fbResult.result), "Failed to create shadow map framebuffer!");
    framebuffer = fbResult.value;

}

void CascadedShadowMap::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path). Must leave the object
    // reusable: VulkanRenderer re-inits this stage when shadow settings change.
    if (!device) { return; }

    spdlog::info("CascadedShadowMap: Destroying pipeline handle: 0x{:x}", (uint64_t)(VkPipeline)graphicsPipeline);
    if (shadowMapArrayView) {
        device->getLogicalDevice().destroyImageView(shadowMapArrayView);
        shadowMapArrayView = nullptr;
    }

    if (framebuffer) {
        device->getLogicalDevice().destroyFramebuffer(framebuffer);
        framebuffer = nullptr;
    }

    if (renderPass) {
        device->getLogicalDevice().destroyRenderPass(renderPass);
        renderPass = nullptr;
    }
    if (graphicsPipeline) {
        device->getLogicalDevice().destroyPipeline(graphicsPipeline);
        graphicsPipeline = nullptr;
    }
    if (pipelineLayout) {
        device->getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        pipelineLayout = nullptr;
    }
    if (descriptorSetLayout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
        descriptorSetLayout = nullptr;
    }
    if (descriptorPool) {
        device->getLogicalDevice().destroyDescriptorPool(descriptorPool);
        descriptorPool = nullptr;
    }
    descriptorSet = nullptr;// freed with the pool
    if (shadowMapArray) {
        shadowMapArray->cleanUp();
        shadowMapArray.reset();
    }
    lightMatricesBuffer.cleanUp();

    device.reset();
}

void CascadedShadowMap::createDescriptorSetAndPipeline()
{
    vk::DescriptorSetLayoutBinding lightMatricesBinding{};
    lightMatricesBinding.binding = 1;  // UNIFORM_LIGHT_MATRICES_BINDING = 1
    lightMatricesBinding.descriptorCount = 1;
    lightMatricesBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    lightMatricesBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &lightMatricesBinding;

    auto layoutRes = device->getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    ASSERT_VULKAN(VkResult(layoutRes.result), "Failed to create shadow map descriptor set layout!");
    descriptorSetLayout = layoutRes.value;

    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eUniformBuffer;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    auto poolRes = device->getLogicalDevice().createDescriptorPool(poolInfo);
    ASSERT_VULKAN(VkResult(poolRes.result), "Failed to create shadow map descriptor pool!");
    descriptorPool = poolRes.value;

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    auto allocRes = device->getLogicalDevice().allocateDescriptorSets(allocInfo);
    ASSERT_VULKAN(VkResult(allocRes.result), "Failed to allocate shadow map descriptor set!");
    descriptorSet = allocRes.value[0];

    std::vector<glm::mat4> lightMatrices(numCascades);
    for (size_t i = 0; i < lightMatrices.size(); i++) {
        lightMatrices[i] = cascadeData[i].viewProjMatrix;
    }

    vk::CommandPool transferCommandPool{};
    vk::CommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolCreateInfo.queueFamilyIndex = static_cast<uint32_t>(device->getQueueFamilies().graphics_family);
    auto poolResult = device->getLogicalDevice().createCommandPool(poolCreateInfo);
    ASSERT_VULKAN(VkResult(poolResult.result), "Failed to create transfer command pool for cascaded shadow map!");
    transferCommandPool = poolResult.value;

    VulkanBufferManager vbm;
    vbm.createBufferAndUploadVectorOnDevice(device, transferCommandPool, lightMatricesBuffer,
        vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        lightMatrices);

    device->getLogicalDevice().destroyCommandPool(transferCommandPool);

    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = lightMatricesBuffer.getBuffer();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(glm::mat4) * numCascades;

    vk::WriteDescriptorSet write{};
    write.dstSet = descriptorSet;
    write.dstBinding = 1;  // UNIFORM_LIGHT_MATRICES_BINDING = 1
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    device->getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
}

void CascadedShadowMap::createGraphicsPipeline()
{
    createDescriptorSetAndPipeline();

    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/rasterizer/shadows/";

    vk::ShaderModule vertModule = loadSpirvShaderModule(device, slang_spv_dir + "shadow_map.shadow_vs_main.spv");
    vk::ShaderModule fragModule = loadSpirvShaderModule(device, slang_spv_dir + "shadow_map.shadow_fs_main.spv");

    vk::PipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    // No geometry stage: the vertex shader selects the cascade matrix by
    // SV_ViewID under multiview (Slang equivalent of gl_ViewIndex).
    vk::PipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";

    std::array skyStages = {vertStageInfo, fragStageInfo};

    vk::VertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = vk::Format::eR32G32B32Sfloat;
    posAttr.offset = 0;

    // UV for the MASK alpha test in the fragment stage. Location 3 = the Vertex
    // texture_coords slot, matching both the shadow and forward vertex shaders.
    vk::VertexInputAttributeDescription uvAttr{};
    uvAttr.location = 3;
    uvAttr.binding = 0;
    uvAttr.format = vk::Format::eR32G32Sfloat;
    uvAttr.offset = offsetof(Vertex, texture_coords);

    vk::PushConstantRange pushConstantRange{};
    // The fragment stage now reads objectIndex from the push block too.
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(uint32_t);

    // set 0 = the shared render descriptor set (materials/textures/object
    // descriptions) the fragment alpha test samples; set 1 = the light matrices
    // this pass owns. The shared layout is owned by VulkanRenderer.
    std::array<vk::DescriptorSetLayout, 2> setLayouts = { sharedRenderDescriptorSetLayout, descriptorSetLayout };

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    auto layoutRes = device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    ASSERT_VULKAN(VkResult(layoutRes.result), "Failed to create shadow map pipeline layout!");
    pipelineLayout = layoutRes.value;

    PipelineBuilder pipelineBuilder;
    graphicsPipeline =
      pipelineBuilder.setShaderStages({ skyStages.begin(), skyStages.end() })
        .setVertexInput({ bindingDesc }, { posAttr, uvAttr })
        // Culling MUST be off here, and it is not an optimisation question.
        //
        // VulkanRenderer flips the camera projection's Y (globalUBO.projection
        // [1][1] *= -1, the usual Vulkan convention), which reverses triangle
        // winding for every pass that uses it. The cascade matrices are built
        // from glm::ortho with no such flip, so under the builder's default
        // (cull back, counter-clockwise front) the shadow pass culled exactly
        // the faces the camera keeps. Measured: the depth map stayed at its
        // 1.0 clear value for 99.8% of sampled texels, so nothing was ever
        // occluded - shadows rendered, but of nothing.
        //
        // Disabling culling rather than flipping the ortho keeps the two
        // conventions from having to agree, and is correct for single-sided
        // geometry like the debug scene's ground plane, which would otherwise
        // cast no shadow from half the sun angles. The cost is rasterising
        // both faces of closed meshes into a depth-only pass.
        .setCullMode(vk::CullModeFlagBits::eNone)
        // Depth clamp = shadow "pancaking". The ortho near plane hugs the
        // camera frustum plus a 10-unit pad, and isVisibleAsShadowCaster
        // deliberately KEEPS casters nearer the light than that (its test pins
        // it) - without clamping, the rasterizer then CLIPPED those casters
        // and a ceiling or overhang cast no shadow at all. Clamping writes
        // them at depth 0 (nearest), which is depth-correct for occlusion.
        // Extending the near plane instead was tried and REGRESSED
        // GoldenRender.ShadowsDarkenSomePixels: the shader bias is constant in
        // NORMALIZED depth (common/cascaded_shadow.slang), so widening the depth range
        // scales the bias in world units and eats contact shadows. Clamp keeps
        // the range tight. Guarded: without the device feature this stays off
        // and such casters clip exactly as before.
        .setDepthClamp(device->supportsDepthClamp())
        .setUseColorBlendState(false)
        .build(device->getLogicalDevice(),
          pipelineLayout,
          renderPass,
          device->getPipelineCache(),
          0,
          "Failed to create shadow map graphics pipeline!");
    spdlog::info("CascadedShadowMap: Created pipeline handle: 0x{:x}", (uint64_t)(VkPipeline)graphicsPipeline);

    device->getLogicalDevice().destroyShaderModule(vertModule);
    device->getLogicalDevice().destroyShaderModule(fragModule);
}

void CascadedShadowMap::recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, Scene *scene, std::span<const vk::DescriptorSet> descriptorSets)
{
    castersDrawn = 0;
    castersConsidered = 0;

    // Single multiview pass: every draw is broadcast to all cascade views and
    // the vertex shader picks the light matrix by gl_ViewIndex. Culling
    // becomes a UNION over the cascade frusta - a caster any cascade can see
    // is drawn (it rasterizes into every view, slightly more conservative
    // than the old per-cascade cull, but the depth results are identical and
    // two pass boundaries plus the geometry stage are gone). The safety
    // property of the old per-cascade test is preserved: geometry outside
    // EVERY cascade's ortho box cannot affect any depth map.
    if (numCascades > MAX_CASCADES) {
        spdlog::error("CascadedShadowMap::recordCommands: numCascades ({}) exceeds MAX_CASCADES ({})", numCascades, MAX_CASCADES);
        return;
    }
    std::array<FrustumPlanes, MAX_CASCADES> cascadeFrusta{};
    for (uint32_t cascade = 0; cascade < numCascades; cascade++) {
        cascadeFrusta[cascade] = extractFrustumPlanes(cascadeData[cascade].viewProjMatrix);
    }

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = vk::Extent2D{shadowWidth, shadowHeight};

    vk::ClearValue clearValue{};
    clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowWidth);
    viewport.height = static_cast<float>(shadowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer.setViewport(0, viewport);

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = vk::Extent2D{shadowWidth, shadowHeight};
    commandBuffer.setScissor(0, scissor);

    // set 0 = the shared render set passed in (materials/textures/object
    // descriptions, for the fragment alpha test); set 1 = this pass's light
    // matrices. descriptorSets is the same span the forward rasterizer receives.
    std::array<vk::DescriptorSet, 2> shadowDescriptorSets = {descriptorSet, descriptorSet};
    const uint32_t shadowDescriptorSetCount = descriptorSets.empty() ? 1 : 2;
    if (!descriptorSets.empty()) { shadowDescriptorSets = {descriptorSets[0], descriptorSet}; }
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
      pipelineLayout,
      0,
      std::span(shadowDescriptorSets.data(), shadowDescriptorSetCount),
      nullptr);

    // objectIndex is the running FLAT mesh index (matching the forward pass and
    // the per-mesh object-description buffer), pushed per mesh in the old
    // cascade-index slot so the fragment stage can fetch this draw's material for
    // the MASK alpha test. Advances for every caster (culled included). The
    // cascade itself comes from gl_ViewIndex. No-op vs the per-model push while a
    // Model holds one Mesh (#10).
    uint32_t flat_mesh_index = 0;
    for (uint32_t m = 0; m < scene->getModelCount(); m++) {
        const glm::mat4 modelMatrix = scene->getModelMatrix(m);

        for (uint32_t k = 0; k < scene->getMeshCount(m); k++) {
            const uint32_t object_index = flat_mesh_index++;
            ++castersConsidered;
            const AABB casterBounds = transformAABB(modelMatrix, scene->getMeshBounds(m, k));
            bool visible_in_any_cascade = false;
            for (uint32_t cascade = 0; cascade < numCascades; cascade++) {
                if (isVisibleAsShadowCaster(cascadeFrusta[cascade], casterBounds)) {
                    visible_in_any_cascade = true;
                    break;
                }
            }
            if (!visible_in_any_cascade) { continue; }
            ++castersDrawn;

            const ShadowPushConstants push = makeShadowPush(modelMatrix, object_index);
            commandBuffer.pushConstants(pipelineLayout,
              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
              0,
              sizeof(ShadowPushConstants),
              &push);

            const vk::Buffer vertex_buffer = scene->getVertexBuffer(m, k);
            const vk::DeviceSize offset = 0;
            commandBuffer.bindVertexBuffers(0, 1, &vertex_buffer, &offset);
            commandBuffer.bindIndexBuffer(scene->getIndexBuffer(m, k), 0, vk::IndexType::eUint32);
            commandBuffer.drawIndexed(scene->getIndexCount(m, k), 1, 0, 0, 0);
        }
    }

    commandBuffer.endRenderPass();
}
}
