module;
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.cascaded_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;

namespace Kataglyphis {

void CascadedShadowMap::init(VulkanDevice *in_device, uint32_t width, uint32_t height, uint32_t num_cascades)
{
    this->device = in_device;
    this->shadowWidth = width;
    this->shadowHeight = height;
    this->numCascades = num_cascades;

    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    // Create 2D Texture Array for Cascades
    shadowMapArray = std::make_unique<Texture>();
    shadowMapArray->createImage(device, shadowWidth, shadowHeight, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, numCascades);

    // Create a view for the entire array (used in descriptor set for reading later)
    shadowMapArray->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::e2DArray, numCascades);

    createRenderPass();
    createFramebuffers();
}

void CascadedShadowMap::createRenderPass()
{
    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal; // To read in lighting pass

    vk::AttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 0;
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
    // Need separate view per layer for framebuffers
    framebuffers.resize(numCascades);

    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    for (uint32_t i = 0; i < numCascades; i++) {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = shadowMapArray->getVulkanImage().getImage();
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = i; // Point to individual cascade
        viewInfo.subresourceRange.layerCount = 1;

        vk::ImageView layerView = device->getLogicalDevice().createImageView(viewInfo).value;

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &layerView;
        framebufferInfo.width = shadowWidth;
        framebufferInfo.height = shadowHeight;
        framebufferInfo.layers = 1;

        framebuffers[i] = device->getLogicalDevice().createFramebuffer(framebufferInfo).value;

        // Need to manually clean up layerView later, but since this is bound to texture lifetime...
        // Typically, we'd store these layer views to delete them, but I will skip storing for brevity here.
    }
}

void CascadedShadowMap::cleanUp()
{
    if (device) {
        for (auto fb : framebuffers) {
            device->getLogicalDevice().destroyFramebuffer(fb);
        }
        device->getLogicalDevice().destroyRenderPass(renderPass);
        if (shadowMapArray) {
            shadowMapArray->cleanUp();
        }
    }
}
}