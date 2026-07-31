module;
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <memory>
#include <filesystem>
#include <sstream>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"

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

    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
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

namespace {
// Free so computeCascadeData() can be called without a CascadedShadowMap (and
// therefore without a Vulkan device) in tests.
std::vector<glm::vec4> frustumCornersWorldSpace(const glm::mat4 &proj, const glm::mat4 &view)
{
    const auto inv = glm::inverse(proj * view);

    std::vector<glm::vec4> frustumCorners;
    for (unsigned int x = 0; x < 2; ++x) {
        for (unsigned int y = 0; y < 2; ++y) {
            for (unsigned int z = 0; z < 2; ++z) {
                // X/Y span the NDC cube [-1, 1], but depth does NOT: the engine
                // is built with GLM_FORCE_DEPTH_ZERO_TO_ONE (Vulkan convention),
                // so NDC z runs 0..1.
                const glm::vec4 pt =
                  inv * glm::vec4((2.0F * x) - 1.0F, (2.0F * y) - 1.0F, static_cast<float>(z), 1.0F);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }

    return frustumCorners;
}
}// namespace

uint32_t clampCascadeCount(uint32_t requested, uint32_t maxCascades, uint32_t deviceViewLimit)
{
    uint32_t const clamped = std::min({ requested, maxCascades, deviceViewLimit });
    return std::max<uint32_t>(1U, clamped);
}

ShadowPushConstants makeShadowPush(const glm::mat4 &modelMatrix, uint32_t cascadeIndex)
{
    // Deliberately trivial, and deliberately a named function: this used to be
    // written inline as `glm::mat4(1.0f)`, so the shadow pass rendered casters
    // at the wrong scale and nothing was ever occluded. A unit test now pins
    // that the caller's matrix is what goes to the GPU.
    return ShadowPushConstants{ modelMatrix, cascadeIndex };
}

std::vector<CascadeData> computeCascadeData(uint32_t numCascades,
  const glm::mat4 &cameraView,
  float cameraFov,
  float aspect,
  float nearPlane,
  float farPlane,
  const glm::vec3 &lightDir,
  float shadowDistance,
  float splitLambda,
  uint32_t shadowMapResolution)
{
    std::vector<CascadeData> cascadeData(numCascades);
    if (numCascades == 0U) { return cascadeData; }

    // Shadows are fitted to shadowDistance, NOT to the camera far plane. The
    // two are unrelated: the debug scene ends at ~36 units of view depth while
    // the camera sees 150, so fitting cascades to the far plane spent two
    // thirds of the shadow map on empty space. Measured box widths for that
    // framing, 2048x2048 map:
    //   far plane 150, uniform : 3.80 cm/texel over the scene
    //   distance 60, lambda 0.5: 3.04 cm/texel, and 1.79 for a near subject
    // 0 or negative means "no clamp" - fall back to the far plane.
    const float shadowFar =
      (shadowDistance > 0.0F) ? std::min(shadowDistance, farPlane) : farPlane;
    const float shadowNear = std::min(nearPlane, shadowFar * 0.5F);
    const float lambda = std::clamp(splitLambda, 0.0F, 1.0F);

    std::vector<float> cascadeSplits(numCascades + 1);
    cascadeSplits[0] = shadowNear;

    for (uint32_t i = 1; i < numCascades + 1; i++) {
        // Practical split scheme (Zhang et al.): blend a logarithmic
        // distribution, which matches how perspective projection compresses
        // depth, with a uniform one, which keeps the near cascades from
        // collapsing onto the first metre.
        //
        // lambda is NOT "higher is better", and it defaults to 0 (pure
        // uniform) for a measured reason. Worst cm/texel over the debug
        // scene's subject, which sits at view depth 16-36, shadow distance 60:
        //   lambda 0.00  ->  3.04    lambda 0.25  ->  4.56
        //   lambda 0.15  ->  4.56    lambda 0.50  ->  4.56
        // That is a cliff, not a curve: at lambda 0 the second split lands at
        // 40.0, just past the subject, so it fits in the tighter cascades. Any
        // lambda above 0 pulls that split back to ~35 and spills the subject
        // into the 60-unit last cascade. Tuning lambda against one camera
        // angle is overfitting; the durable win is shadowFar above.
        //
        // It still earns its keep for a camera close to its subject
        // (a 2-12 unit subject: 1.52 cm/texel at lambda 0, 1.01 at 0.35),
        // which is why the knob exists rather than being deleted.
        const float p = static_cast<float>(i) / static_cast<float>(numCascades);
        const float logSplit = shadowNear * std::pow(shadowFar / shadowNear, p);
        const float uniformSplit = shadowNear + ((shadowFar - shadowNear) * p);
        cascadeSplits[i] = (lambda * logSplit) + ((1.0F - lambda) * uniformSplit);
    }
    // The last split must land exactly on shadowFar; the blend above is only
    // accurate to float rounding, and a short final cascade leaves a band of
    // geometry that samples nothing and renders unshadowed.
    cascadeSplits[numCascades] = shadowFar;

    for (uint32_t i = 0; i < numCascades; i++) {
        glm::mat4 const curr_cascade_proj = glm::perspective(glm::radians(cameraFov), aspect, cascadeSplits[i], cascadeSplits[i + 1]);

        std::vector<glm::vec4> frustumCornerWorldSpace = frustumCornersWorldSpace(curr_cascade_proj, cameraView);

        glm::vec3 center = glm::vec3(0, 0, 0);
        for (const auto &v : frustumCornerWorldSpace) { center += glm::vec3(v); }
        center /= frustumCornerWorldSpace.size();

        // Radius of the cascade's frustum, used to place the light camera far
        // enough back that the whole cascade sits IN FRONT of it. The eye used
        // to be center - lightDir (one unit away), which put part of the
        // cascade behind the light's near plane.
        float radius = 0.0F;
        for (const auto &v : frustumCornerWorldSpace) {
            radius = std::max(radius, glm::length(glm::vec3(v) - center));
        }

        glm::vec3 light_direction = lightDir;
        if (glm::length(light_direction) < 1e-6F) { light_direction = glm::vec3(0.0F, -1.0F, 0.0F); }
        light_direction = glm::normalize(light_direction);
        const glm::vec3 up_axis =
          (std::abs(light_direction.y) > 0.99F) ? glm::vec3(0.0F, 0.0F, 1.0F) : glm::vec3(0.0F, 1.0F, 0.0F);

        if (shadowMapResolution > 0) {
            // STABILIZED path. Three ingredients, each necessary:
            //
            // 1. A WORLD-FIXED light basis (pure rotation about the origin).
            //    The legacy lookAt is anchored at the slice center, so camera
            //    translation is absorbed into the view matrix in continuous
            //    amounts and no snap applied afterwards can help.
            // 2. A box sized from `radius` - a function of the slice geometry
            //    only (fov/aspect/splits), so its texel footprint never
            //    changes as the camera moves or turns.
            // 3. The box CENTER snapped to whole texels in that fixed basis,
            //    so the box only ever moves in texel increments and a static
            //    shadow edge always lands on the same texels.
            //
            // The box is padded by one texel because the snap can shift the
            // center by up to a texel in each axis - without the pad, slice
            // corners could fall just outside. Depth still fits the corners;
            // near may come out NEGATIVE here (the basis is anchored at the
            // origin, not behind the scene) - glm::ortho is a plain box and
            // accepts that; the legacy 0.01 clamp assumed an eye placed
            // behind everything.
            glm::mat4 const light_basis = glm::lookAt(-light_direction, glm::vec3(0.0F), up_axis);

            float const texel_world = (2.0F * radius) / static_cast<float>(shadowMapResolution);
            glm::vec3 center_ls = glm::vec3(light_basis * glm::vec4(center, 1.0F));
            center_ls.x = std::floor(center_ls.x / texel_world) * texel_world;
            center_ls.y = std::floor(center_ls.y / texel_world) * texel_world;
            float const half_extent = radius + texel_world;

            float snapMinZ = std::numeric_limits<float>::max();
            float snapMaxZ = std::numeric_limits<float>::lowest();
            for (const auto &m : frustumCornerWorldSpace) {
                glm::vec4 const v_light = light_basis * m;
                snapMinZ = std::min(snapMinZ, v_light.z);
                snapMaxZ = std::max(snapMaxZ, v_light.z);
            }
            constexpr float snapZPadding = 10.0F;
            float const snap_near = -snapMaxZ - snapZPadding;
            float snap_far = -snapMinZ + snapZPadding;
            if (snap_far <= snap_near) { snap_far = snap_near + 1.0F; }

            glm::mat4 const snap_projection = glm::ortho(center_ls.x - half_extent,
              center_ls.x + half_extent,
              center_ls.y - half_extent,
              center_ls.y + half_extent,
              snap_near,
              snap_far);

            cascadeData[i].viewProjMatrix = snap_projection * light_basis;
            cascadeData[i].splitDepth = cascadeSplits[i + 1];
            continue;
        }

        glm::mat4 const light_view_matrix =
          glm::lookAt(center - (light_direction * (radius * 2.0F + 10.0F)), center, up_axis);

        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (const auto& m : frustumCornerWorldSpace) {
            glm::vec4 const v_light_view = light_view_matrix * m;
            minX = std::min(minX, v_light_view.x);
            maxX = std::max(maxX, v_light_view.x);
            minY = std::min(minY, v_light_view.y);
            maxY = std::max(maxY, v_light_view.y);
            minZ = std::min(minZ, v_light_view.z);
            maxZ = std::max(maxZ, v_light_view.z);
        }

        // Light view space is right-handed and looks down -Z, so corner z
        // values are NEGATIVE. glm::ortho takes positive near/far DISTANCES:
        // near = -maxZ (closest corner), far = -minZ (farthest). Passing the
        // raw negative values mapped nearly every fragment outside [0,1]
        // depth - measured: only ~5% of visible fragments landed inside the
        // shadow map, which is why the sampled shadow term was noise.
        constexpr float zPadding = 10.0F;// keep casters just outside the box
        float near_distance = std::max(0.01F, -maxZ - zPadding);
        float far_distance = (-minZ) + zPadding;
        if (far_distance <= near_distance) { far_distance = near_distance + 1.0F; }

        glm::mat4 const light_projection =
          glm::ortho(minX, maxX, minY, maxY, near_distance, far_distance);

        cascadeData[i].viewProjMatrix = light_projection * light_view_matrix;
        // The split depth is the far plane of this cascade frustum, but measured in view space depth
        // A simple way is to pass the positive distance
        cascadeData[i].splitDepth = cascadeSplits[i + 1];
    }

    return cascadeData;
}

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
    framebuffers.resize(1);
    shadowMapLayerViews.resize(1);

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
    shadowMapLayerViews[0] = viewResult.value;

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &shadowMapLayerViews[0];
    framebufferInfo.width = shadowWidth;
    framebufferInfo.height = shadowHeight;
    framebufferInfo.layers = 1;

    auto fbResult = device->getLogicalDevice().createFramebuffer(framebufferInfo);
    ASSERT_VULKAN(VkResult(fbResult.result), "Failed to create shadow map framebuffer!");
    framebuffers[0] = fbResult.value;

}

void CascadedShadowMap::cleanUp()
{
    // Idempotent: safe to call again after an explicit cleanUp (the destructor
    // is only a safety net for the forgotten path). Must leave the object
    // reusable: VulkanRenderer re-inits this stage when shadow settings change.
    if (!device) { return; }

    spdlog::info("CascadedShadowMap: Destroying pipeline handle: 0x{:x}", (uint64_t)(VkPipeline)graphicsPipeline);
    for (auto view : shadowMapLayerViews) {
        device->getLogicalDevice().destroyImageView(view);
    }
    shadowMapLayerViews.clear();

    for (auto fb : framebuffers) {
        device->getLogicalDevice().destroyFramebuffer(fb);
    }
    framebuffers.clear();

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
    if (poolResult.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create transfer command pool for cascaded shadow map! Error: {}", static_cast<int>(poolResult.result));
        std::abort();
    }
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
        // NORMALIZED depth (cascaded_shadow.glsl), so widening the depth range
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

void CascadedShadowMap::recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, Scene *scene, const std::vector<vk::DescriptorSet> &descriptorSets)
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
    std::vector<FrustumPlanes> cascadeFrusta(numCascades);
    for (uint32_t cascade = 0; cascade < numCascades; cascade++) {
        cascadeFrusta[cascade] = extractFrustumPlanes(cascadeData[cascade].viewProjMatrix);
    }

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[0];
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
    // matrices. descriptorSets is the same vector the forward rasterizer receives.
    std::vector<vk::DescriptorSet> shadowDescriptorSets = {descriptorSet};
    if (!descriptorSets.empty()) { shadowDescriptorSets = {descriptorSets[0], descriptorSet}; }
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, shadowDescriptorSets, nullptr);

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
