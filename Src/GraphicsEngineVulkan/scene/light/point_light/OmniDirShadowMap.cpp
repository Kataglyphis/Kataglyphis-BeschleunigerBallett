module;
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"

#include <unordered_map>

module kataglyphis.vulkan.omni_dir_shadow_map;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.texture;

namespace Kataglyphis {

static std::unordered_map<OmniDirShadowMap*, vk::ImageView> g_layerViewMap;

void OmniDirShadowMap::init(std::shared_ptr<VulkanDevice>in_device, uint32_t width, uint32_t height)
{
    this->device = in_device;
    this->shadowWidth = width;
    this->shadowHeight = height;

    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    // Create Cube Map
    shadowMapCube = std::make_unique<Texture>();
    shadowMapCube->createImage(device, shadowWidth, shadowHeight, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, 6, vk::ImageCreateFlagBits::eCubeCompatible);

    shadowMapCube->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1, vk::ImageViewType::eCube, 6);

    createRenderPass();
    createFramebuffers();
}

void OmniDirShadowMap::createRenderPass()
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
    depthAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Using Multiview for rendering to 6 layers at once
    uint32_t viewMask = 0b00111111; // 6 layers
    vk::RenderPassMultiviewCreateInfo multiviewInfo{};
    multiviewInfo.subpassCount = 1;
    multiviewInfo.pViewMasks = &viewMask;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.pNext = &multiviewInfo;

    auto result = device->getLogicalDevice().createRenderPass(renderPassInfo);
    ASSERT_VULKAN(VkResult(result.result), "Failed to create omni dir shadow map render pass!");
    renderPass = result.value;
}

void OmniDirShadowMap::createFramebuffers()
{
    // Need view that spans all 6 layers
    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = shadowMapCube->getVulkanImage().getImage();
    viewInfo.viewType = vk::ImageViewType::e2DArray; // Multiview expects 2DArray
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vk::ImageView view = device->getLogicalDevice().createImageView(viewInfo).value;
    g_layerViewMap[this] = view;

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &g_layerViewMap[this];
    framebufferInfo.width = shadowWidth;
    framebufferInfo.height = shadowHeight;
    framebufferInfo.layers = 1; // Handled by Multiview viewMask

    framebuffer = device->getLogicalDevice().createFramebuffer(framebufferInfo).value;
}

void OmniDirShadowMap::cleanUp()
{
    if (device) {
        if (g_layerViewMap.find(this) != g_layerViewMap.end()) {
            device->getLogicalDevice().destroyImageView(g_layerViewMap[this]);
            g_layerViewMap.erase(this);
        }
        device->getLogicalDevice().destroyFramebuffer(framebuffer);
        device->getLogicalDevice().destroyRenderPass(renderPass);
        if (shadowMapCube) {
            shadowMapCube->cleanUp();
            shadowMapCube.reset();
        }
    }
}
}
