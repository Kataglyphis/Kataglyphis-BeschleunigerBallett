module;
#include <vector>
#include <unordered_map>
#include <memory>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.cascaded_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;

namespace Kataglyphis {

// Global map to store per-layer image views
static std::unordered_map<CascadedShadowMap*, std::vector<vk::ImageView>> g_layerViewsMap;

void CascadedShadowMap::init(VulkanDevice *in_device, uint32_t width, uint32_t height, uint32_t num_cascades)
{
    this->device = in_device;
    this->shadowWidth = width;
    this->shadowHeight = height;
    this->numCascades = num_cascades;
    
    cascadeData.resize(numCascades);

    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    // Create 2D Texture Array for Cascades
    shadowMapArray = new Texture();
    shadowMapArray->createImage(device, shadowWidth, shadowHeight, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, numCascades);

    // Create a view for the entire array (used in descriptor set for reading later)
    shadowMapArray->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray, numCascades);

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
        if (shadowMapArray) {
            shadowMapArray->cleanUp();
            delete shadowMapArray;
        }
    }
    }
}
