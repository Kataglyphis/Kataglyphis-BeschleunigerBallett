module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
#include "common/FramebufferHelper.hpp"
#include "common/PipelineLayoutHelper.hpp"
#include "common/RenderPassHelper.hpp"
#include "common/Utilities.hpp"
#include "common/ViewportHelper.hpp"
#include "renderer/SceneUBO.hpp"

module kataglyphis.vulkan.cascaded_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.pipeline_builder;

namespace Kataglyphis {


void CascadedShadowMap::init(std::shared_ptr<VulkanDevice>in_device, uint32_t width, uint32_t height, uint32_t num_cascades,
  vk::DescriptorSetLayout sharedRenderDescriptorSetLayout, uint32_t swapChainImageCount,
  vk::CommandPool commandPool)
{
    this->device = in_device;
    this->shadowWidth = width;
    this->shadowHeight = height;
    this->numCascades = num_cascades;
    this->sharedRenderDescriptorSetLayout = sharedRenderDescriptorSetLayout;
    this->swapChainImageCount = swapChainImageCount;
    this->commandPool = commandPool;

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
    // Sampled view: exactly one aspect, not Kataglyphis::depthStencilTransitionAspect. See its doc comment.
    shadowMapArray->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray, numCascades);
    // Comparison sampler: paired with the Sampler2DArrayShadow binding in
    // common/cascaded_shadow.slang, linear filtering gives a free 2x2 PCF tap
    // via SampleCmpLevelZero instead of thresholding a bilinear-filtered raw
    // depth sample. eLessOrEqual matches the manual `(currentDepth - bias) >
    // mapDepth` compare it replaces (visible when Dref <= stored depth) and
    // the Rust renderer's equivalent CompareFunction::LessEqual
    // (render/forward.rs, shadow_sampler).
    shadowMapArray->createTextureSampler(
      device, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, VK_TRUE, vk::CompareOp::eLessOrEqual);

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
    // This runs once per frame, so it must not allocate. cascadeData is sized
    // exactly once, by init() (cascadeData.resize(numCascades)), and the only
    // way numCascades can change is another init() via
    // VulkanRenderer::handleShadowResolutionChange - so the frame path only
    // ever WRITES into storage that is already the right size. Assert that
    // instead of resizing here; a resize on the frame path would mean the
    // invariant broke somewhere upstream.
    if (cascadeData.size() != numCascades) {
        spdlog::error("CascadedShadowMap::updateCascades: cascadeData ({}) not sized for numCascades ({}); init() must size it",
          cascadeData.size(),
          numCascades);
        return;
    }
    computeCascadeDataInto(cascadeData,
      numCascades,
      cameraView,
      cameraFov,
      aspect,
      nearPlane,
      farPlane,
      lightDir,
      shadowDistance,
      splitLambda,
      shadowWidth);
}

void CascadedShadowMap::uploadLightMatrices(uint32_t image_index)
{
    // Push the freshly computed matrices into the UBO the shadow geometry
    // shader renders with. Without this the buffer keeps whatever was in
    // cascadeData when createGraphicsPipeline() ran at init - i.e. default
    // constructed matrices - so the shadow map was rendered from a garbage
    // viewpoint while the lighting shader sampled it with the correct ones.
    //
    // One buffer per swapchain image (like globalUBOBuffer/sceneUBOBuffer):
    // called once per image_index from update_uniform_buffers, so the CPU
    // never rewrites a buffer an in-flight shadow pass for a DIFFERENT image
    // may still be reading.
    //
    // Silently no-op (not an error) while empty: VulkanRenderer's very first
    // create_uniform_buffers() pass runs before dirShadowMap.init() has ever
    // been called, so every image is seeded again once the pipeline exists -
    // see the loop after updateUniforms() in VulkanRenderer::init(). Once
    // non-empty, an out-of-range index is a genuine caller bug.
    if (lightMatricesBuffers.empty()) { return; }
    if (image_index >= lightMatricesBuffers.size()) {
        spdlog::error("CascadedShadowMap::uploadLightMatrices: image_index ({}) exceeds buffer count ({})",
          image_index, lightMatricesBuffers.size());
        return;
    }

    // Written cascade by cascade, straight into the mapped buffer: the staging
    // std::vector<glm::mat4> this replaced was a heap allocation per frame for
    // a copy that is thrown away one line later. The buffer holds exactly
    // numCascades matrices (createDescriptorSetAndPipeline uploads that many).
    if (void *mapped = lightMatricesBuffers[image_index].getMappedData(); mapped != nullptr) {
        auto *const matrices = static_cast<std::byte *>(mapped);
        for (size_t i = 0; i < cascadeData.size(); i++) {
            std::memcpy(matrices + (i * sizeof(glm::mat4)), &cascadeData[i].viewProjMatrix, sizeof(glm::mat4));
        }
    }
}

void CascadedShadowMap::createRenderPass()
{
    // Stored, not eDontCare: the lighting pass samples this depth map as the
    // shadow map, so its final layout is eShaderReadOnlyOptimal.
    const vk::AttachmentDescription depthAttachment =
      buildAttachmentDescription(depth_format, vk::ImageLayout::eShaderReadOnlyOptimal);

    vk::AttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    const vk::SubpassDescription subpass =
      buildSubpassDescription(std::span<const vk::AttachmentReference>{}, &depthAttachmentRef);

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

    const std::array<vk::AttachmentDescription, 1> attachments = { depthAttachment };
    const std::array<vk::SubpassDescription, 1> subpasses = { subpass };

    vk::RenderPassCreateInfo renderPassInfo =
      Kataglyphis::buildRenderPassCreateInfo(attachments, subpasses, dependencies);
    renderPassInfo.pNext = &multiviewInfo;

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
    //
    // Reuses shadowMapArray's own sampled view instead of creating a second,
    // byte-identical one for the framebuffer attachment: both used to pass
    // the exact same (format, eDepth, 1, e2DArray, numCascades) tuple to the
    // image-view create info.
    const vk::ImageView attachment = shadowMapArray->getImageView();
    const vk::FramebufferCreateInfo framebufferInfo = Kataglyphis::buildFramebufferCreateInfo(
      renderPass, std::span<const vk::ImageView>(&attachment, 1), vk::Extent2D{ shadowWidth, shadowHeight });

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

    Kataglyphis::destroyFramebuffer(device->getLogicalDevice(), framebuffer);

    if (renderPass) {
        device->getLogicalDevice().destroyRenderPass(renderPass);
        renderPass = nullptr;
    }
    Kataglyphis::destroyPipelineAndLayout(device->getLogicalDevice(), graphicsPipeline, pipelineLayout);
    lightMatricesDescriptors.cleanUp();
    if (shadowMapArray) {
        shadowMapArray->cleanUp();
        shadowMapArray.reset();
    }
    for (auto &buffer : lightMatricesBuffers) { buffer.cleanUp(); }
    lightMatricesBuffers.clear();

    device.reset();
}

void CascadedShadowMap::createDescriptorSetAndPipeline()
{
    // UNIFORM_LIGHT_MATRICES_BINDING = 1
    lightMatricesDescriptors.addBinding(1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex);
    if (!lightMatricesDescriptors.create(device, swapChainImageCount)) {
        spdlog::error("Failed to create shadow map descriptor resources!");
    }

    // One buffer per swapchain image, like globalUBOBuffer/sceneUBOBuffer -
    // uploadLightMatrices() rewrites lightMatricesBuffers[image_index] every
    // frame, independently of whichever other image's shadow pass is still
    // in flight. Host-visible and written directly through getMappedData(),
    // the same way uploadLightMatrices() (:149-154) rewrites it every frame -
    // so seeding it via a staging buffer + transfer would only duplicate data
    // that path immediately overwrites.
    lightMatricesBuffers.resize(swapChainImageCount);
    for (uint32_t i = 0; i < swapChainImageCount; i++) {
        lightMatricesBuffers[i].create(device, sizeof(glm::mat4) * numCascades,
          vk::BufferUsageFlagBits::eUniformBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        // UNIFORM_LIGHT_MATRICES_BINDING = 1
        lightMatricesDescriptors.writeBuffer(i, 1, lightMatricesBuffers[i].getBuffer(), sizeof(glm::mat4) * numCascades);
    }
    // Seed every image's buffer with the current cascade data now, rather than
    // duplicating uploadLightMatrices()'s memcpy loop here.
    for (uint32_t i = 0; i < swapChainImageCount; i++) { uploadLightMatrices(i); }
}

void CascadedShadowMap::createGraphicsPipeline()
{
    createDescriptorSetAndPipeline();
    buildGraphicsPipeline();
}

void CascadedShadowMap::shaderHotReload()
{
    Kataglyphis::destroyPipelineAndLayout(device->getLogicalDevice(), graphicsPipeline, pipelineLayout);
    // Rebuilds only the pipeline/layout, not lightMatricesDescriptors or its
    // buffer - createDescriptorSetAndPipeline() re-uploads the light matrices
    // buffer and would leak the previous descriptor set + buffer if called
    // again here, and a reload changes shaders, not cascade data.
    buildGraphicsPipeline();
}

void CascadedShadowMap::buildGraphicsPipeline()
{
    // Slang-emitted SPIR-V: compiled by compile-slang-shaders.ps1 at build time.
    // Run from the repo root (per AGENTS.md).
    std::string const slang_spv_dir = "Resources/ShadersSlang/build/spirv/rasterizer/shadows/";

    // No geometry stage: the vertex shader selects the cascade matrix by
    // SV_ViewID under multiview (Slang equivalent of gl_ViewIndex).
    ShaderStagePair stages{ device, slang_spv_dir + "shadow_map.shadow_vs_main.spv",
        slang_spv_dir + "shadow_map.shadow_fs_main.spv" };

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
    std::array<vk::DescriptorSetLayout, 2> setLayouts = { sharedRenderDescriptorSetLayout, lightMatricesDescriptors.getLayout() };
    const std::array<vk::PushConstantRange, 1> pushConstantRanges = { pushConstantRange };

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = buildPipelineLayoutCreateInfo(setLayouts, pushConstantRanges);

    auto layoutRes = device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    ASSERT_VULKAN(VkResult(layoutRes.result), "Failed to create shadow map pipeline layout!");
    pipelineLayout = layoutRes.value;

    PipelineBuilder pipelineBuilder;
    graphicsPipeline =
      pipelineBuilder.setShaderStages({ stages.stages().begin(), stages.stages().end() })
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
}

void CascadedShadowMap::recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, Scene *scene, std::span<const vk::DescriptorSet> descriptorSets, bool cullingEnabled)
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
    if (image_index >= lightMatricesDescriptors.sets().size()) {
        spdlog::error("CascadedShadowMap::recordCommands: image_index ({}) exceeds descriptor set count ({})",
          image_index, lightMatricesDescriptors.sets().size());
        return;
    }
    std::array<FrustumPlanes, MAX_CASCADES> cascadeFrusta{};
    if (cullingEnabled) {
        for (uint32_t cascade = 0; cascade < numCascades; cascade++) {
            cascadeFrusta[cascade] = extractFrustumPlanes(cascadeData[cascade].viewProjMatrix);
        }
    }

    std::array<vk::ClearValue, 1> clearValues{};
    clearValues[0].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    const vk::RenderPassBeginInfo renderPassInfo = Kataglyphis::buildRenderPassBeginInfo(
      renderPass, framebuffer, vk::Extent2D{shadowWidth, shadowHeight}, clearValues);

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

    setFullExtentViewportAndScissor(commandBuffer, vk::Extent2D{ shadowWidth, shadowHeight });

    // set 0 = the shared render set passed in (materials/textures/object
    // descriptions, for the fragment alpha test); set 1 = this pass's light
    // matrices. descriptorSets is the same span the forward rasterizer receives.
    // The pipeline layout (:351 above) always describes set 0 = shared, set 1 =
    // light matrices, so the light matrices set must land at index 1 whether or
    // not the shared set is bound - shadowSetBinding is what keeps firstSet in
    // agreement with that layout.
    const vk::DescriptorSet lightMatricesSet = lightMatricesDescriptors.sets()[image_index];
    if (descriptorSets.empty()) {
        spdlog::warn("CascadedShadowMap::recordCommands: no shared render set bound; the fragment alpha test will "
                     "sample nothing (degraded mode)");
    }
    const std::array<vk::DescriptorSet, 2> sets =
      descriptorSets.empty() ? std::array<vk::DescriptorSet, 2>{ lightMatricesSet, VK_NULL_HANDLE }
                              : std::array<vk::DescriptorSet, 2>{ descriptorSets[0], lightMatricesSet };
    const ShadowSetBinding binding = shadowSetBinding(!descriptorSets.empty());
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
      pipelineLayout,
      binding.firstSet,
      std::span(sets.data(), binding.setCount),
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

            // Unreachable with a null mesh: m/k come from getModelCount()/
            // getMeshCount(m), so findMesh() always resolves here. Kept as a
            // fallback rather than an assert so behaviour matches the former
            // per-accessor calls byte-for-byte.
            Mesh *mesh = scene->findMesh(m, k);
            static const AABB unknownBounds{ glm::vec3(1.0F), glm::vec3(-1.0F) };
            const AABB &meshBounds = mesh != nullptr ? mesh->getBounds() : unknownBounds;
            const AABB casterBounds = transformAABB(modelMatrix, meshBounds);
            bool visible_in_any_cascade = !cullingEnabled;
            if (cullingEnabled) {
                for (uint32_t cascade = 0; cascade < numCascades; cascade++) {
                    if (isVisibleAsShadowCaster(cascadeFrusta[cascade], casterBounds)) {
                        visible_in_any_cascade = true;
                        break;
                    }
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

            const vk::Buffer vertex_buffer = mesh != nullptr ? mesh->getVertexBuffer() : vk::Buffer{};
            const vk::DeviceSize offset = 0;
            commandBuffer.bindVertexBuffers(0, 1, &vertex_buffer, &offset);
            commandBuffer.bindIndexBuffer(
              mesh != nullptr ? mesh->getIndexBuffer() : vk::Buffer{}, 0, vk::IndexType::eUint32);
            commandBuffer.drawIndexed(mesh != nullptr ? mesh->getIndexCount() : 0, 1, 0, 0, 0);
        }
    }

    commandBuffer.endRenderPass();
}
}
