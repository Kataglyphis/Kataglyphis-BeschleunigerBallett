module;
#include <vector>
#include <unordered_map>
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
import kataglyphis.vulkan.file;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.pipeline_builder;

namespace Kataglyphis {

// Global map to store per-layer image views
static std::unordered_map<CascadedShadowMap*, std::vector<vk::ImageView>> g_layerViewsMap;

void CascadedShadowMap::init(std::shared_ptr<VulkanDevice>in_device, uint32_t width, uint32_t height, uint32_t num_cascades)
{
    this->device = in_device;
    this->shadowWidth = width;
    this->shadowHeight = height;
    this->numCascades = num_cascades;
    
    cascadeData.resize(numCascades);

    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    // Create 2D Texture Array for Cascades
    shadowMapArray = std::make_unique<Texture>();
    shadowMapArray->createImage(device, shadowWidth, shadowHeight, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, numCascades);

    // Create a view for the entire array (used in descriptor set for reading later)
    shadowMapArray->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray, numCascades);
    shadowMapArray->createTextureSampler(device, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge);

    createRenderPass();
    createFramebuffers();
}

std::vector<glm::vec4> CascadedShadowMap::getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view)
{
    const auto inv = glm::inverse(proj * view);

    std::vector<glm::vec4> frustumCorners;
    for (unsigned int x = 0; x < 2; ++x) {
        for (unsigned int y = 0; y < 2; ++y) {
            for (unsigned int z = 0; z < 2; ++z) {
                const glm::vec4 pt = inv * glm::vec4((2.0F * x) - 1.0F, (2.0F * y) - 1.0F, (2.0F * z) - 1.0F, 1.0F);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }

    return frustumCorners;
}

void CascadedShadowMap::updateCascades(const glm::mat4& cameraView, float cameraFov, float aspect, float nearPlane, float farPlane, const glm::vec3& lightDir)
{
    std::vector<float> cascadeSplits(numCascades + 1);

    for (uint32_t i = 0; i < numCascades + 1; i++) {
        if (i == 0) {
            cascadeSplits[i] = nearPlane;
        } else {
            // Using a simple uniform split for now; could be changed to practical split scheme
            cascadeSplits[i] = farPlane * (static_cast<float>(i) / static_cast<float>(numCascades));
        }
    }

    for (uint32_t i = 0; i < numCascades; i++) {
        glm::mat4 const curr_cascade_proj = glm::perspective(glm::radians(cameraFov), aspect, cascadeSplits[i], cascadeSplits[i + 1]);

        std::vector<glm::vec4> frustumCornerWorldSpace = getFrustumCornersWorldSpace(curr_cascade_proj, cameraView);

        glm::vec3 center = glm::vec3(0, 0, 0);
        for (const auto &v : frustumCornerWorldSpace) { center += glm::vec3(v); }
        center /= frustumCornerWorldSpace.size();

        glm::mat4 const light_view_matrix = glm::lookAt(center - lightDir, center, glm::vec3(0.0F, 1.0F, 0.0F));

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

        constexpr float zMult = 10.0F;
        if (minZ < 0) minZ *= zMult; else minZ /= zMult;
        if (maxZ < 0) maxZ /= zMult; else maxZ *= zMult;

        glm::mat4 const light_projection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

        cascadeData[i].viewProjMatrix = light_projection * light_view_matrix;
        // The split depth is the far plane of this cascade frustum, but measured in view space depth
        // A simple way is to pass the positive distance
        cascadeData[i].splitDepth = cascadeSplits[i + 1];
    }
}

void CascadedShadowMap::createRenderPass()
{
    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = vk::Format::eD32Sfloat;
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

    vk::RenderPassCreateInfo renderPassInfo{};
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
    framebuffers.resize(numCascades);
    
    // Create layer views for rendering to individual layers
    std::vector<vk::ImageView> layerViews(numCascades);

    for (uint32_t i = 0; i < numCascades; i++) {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = shadowMapArray->getImage();
        viewInfo.viewType = vk::ImageViewType::e2DArray;
        viewInfo.format = vk::Format::eD32Sfloat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = i; // Point to the specific layer
        viewInfo.subresourceRange.layerCount = 1;

        auto viewResult = device->getLogicalDevice().createImageView(viewInfo);
        ASSERT_VULKAN(VkResult(viewResult.result), "Failed to create shadow map layer view!");
        layerViews[i] = viewResult.value;

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &layerViews[i];
        framebufferInfo.width = shadowWidth;
        framebufferInfo.height = shadowHeight;
        framebufferInfo.layers = 1;

        auto fbResult = device->getLogicalDevice().createFramebuffer(framebufferInfo);
        ASSERT_VULKAN(VkResult(fbResult.result), "Failed to create shadow map framebuffer!");
        framebuffers[i] = fbResult.value;
    }
    
    // Store layer views to clean them up later
    g_layerViewsMap[this] = std::move(layerViews);
}

void CascadedShadowMap::cleanUp()
{
    if (device) {
        spdlog::info("CascadedShadowMap: Destroying pipeline handle: 0x{:x}", (uint64_t)(VkPipeline)graphicsPipeline);
        auto it = g_layerViewsMap.find(this);
        if (it != g_layerViewsMap.end()) {
            for (auto view : it->second) {
                device->getLogicalDevice().destroyImageView(view);
            }
            g_layerViewsMap.erase(it);
        }

        for (auto fb : framebuffers) {
            device->getLogicalDevice().destroyFramebuffer(fb);
        }
        framebuffers.clear();

        device->getLogicalDevice().destroyRenderPass(renderPass);
        if (graphicsPipeline) { device->getLogicalDevice().destroyPipeline(graphicsPipeline); }
        if (pipelineLayout) { device->getLogicalDevice().destroyPipelineLayout(pipelineLayout); }
        if (descriptorSetLayout) { device->getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout); }
        if (descriptorPool) { device->getLogicalDevice().destroyDescriptorPool(descriptorPool); }
        if (shadowMapArray) {
            shadowMapArray->cleanUp();
            shadowMapArray.reset();
        }
        lightMatricesBuffer.cleanUp();
    }
}

void CascadedShadowMap::createDescriptorSetAndPipeline()
{
    vk::DescriptorSetLayoutBinding lightMatricesBinding{};
    lightMatricesBinding.binding = 1;  // UNIFORM_LIGHT_MATRICES_BINDING = 1
    lightMatricesBinding.descriptorCount = 1;
    lightMatricesBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    lightMatricesBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry;

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

    std::stringstream shadow_shader_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    shadow_shader_dir << cwd.string() << RELATIVE_RESOURCE_PATH << "Shaders/rasterizer/shadows/";

    ShaderHelper shaderHelper;
    shaderHelper.compileShader(shadow_shader_dir.str(), "directional_shadow_map.vert");
    shaderHelper.compileShader(shadow_shader_dir.str(), "directional_shadow_map.geom");
    shaderHelper.compileShader(shadow_shader_dir.str(), "directional_shadow_map.frag");

    File vertFile(shaderHelper.getShaderSpvDir(shadow_shader_dir.str(), "directional_shadow_map.vert"));
    File geomFile(shaderHelper.getShaderSpvDir(shadow_shader_dir.str(), "directional_shadow_map.geom"));
    File fragFile(shaderHelper.getShaderSpvDir(shadow_shader_dir.str(), "directional_shadow_map.frag"));

    vk::ShaderModule vertModule = shaderHelper.createShaderModule(device, vertFile.readCharSequence());
    vk::ShaderModule geomModule = shaderHelper.createShaderModule(device, geomFile.readCharSequence());
    vk::ShaderModule fragModule = shaderHelper.createShaderModule(device, fragFile.readCharSequence());

    vk::PipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo geomStageInfo{};
    geomStageInfo.stage = vk::ShaderStageFlagBits::eGeometry;
    geomStageInfo.module = geomModule;
    geomStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";

    std::array skyStages = {vertStageInfo, geomStageInfo, fragStageInfo};

    vk::VertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = vk::Format::eR32G32B32Sfloat;
    posAttr.offset = 0;

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    auto layoutRes = device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    ASSERT_VULKAN(VkResult(layoutRes.result), "Failed to create shadow map pipeline layout!");
    pipelineLayout = layoutRes.value;

    PipelineBuilder pipelineBuilder;
    graphicsPipeline =
      pipelineBuilder.setShaderStages({ skyStages.begin(), skyStages.end() })
        .setVertexInput({ bindingDesc }, { posAttr })
        .setUseColorBlendState(false)
        .build(
          device->getLogicalDevice(), pipelineLayout, renderPass, 0, "Failed to create shadow map graphics pipeline!");
    spdlog::info("CascadedShadowMap: Created pipeline handle: 0x{:x}", (uint64_t)(VkPipeline)graphicsPipeline);

    device->getLogicalDevice().destroyShaderModule(vertModule);
    device->getLogicalDevice().destroyShaderModule(geomModule);
    device->getLogicalDevice().destroyShaderModule(fragModule);
}

void CascadedShadowMap::recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, Scene *scene, const std::vector<vk::DescriptorSet> &descriptorSets)
{
    for (uint32_t cascade = 0; cascade < numCascades; cascade++) {
        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[cascade];
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

        std::vector<vk::DescriptorSet> shadowDescriptorSets = {descriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, shadowDescriptorSets, nullptr);

        glm::mat4 modelMatrix = glm::mat4(1.0f);
        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &modelMatrix);

        for (uint32_t m = 0; m < scene->getModelCount(); m++) {
            for (uint32_t k = 0; k < scene->getMeshCount(m); k++) {
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
}
