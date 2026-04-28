module;
#include <memory>
#include <array>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <stb_image.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include "shared/scene/Vertex.hpp"
#include "shared/scene/ObjMaterial.hpp"

#include "common/Utilities.hpp"

module kataglyphis.vulkan.sky_box;

import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.file;
import kataglyphis.vulkan.shader_helper;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.command_buffer_manager;

namespace Kataglyphis {

SkyBox::SkyBox() = default;

void SkyBox::init(std::shared_ptr<VulkanDevice>in_device, vk::CommandPool commandPool)
{
    this->device = in_device;

    createMesh(commandPool);
    loadCubeMap(commandPool);
}

void SkyBox::loadCubeMap(vk::CommandPool commandPool)
{
    std::stringstream skybox_base_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    skybox_base_dir << cwd.string();
    skybox_base_dir << "/Resources/Textures/Skybox/DOOM2016/";

    std::array<std::string, 6> skybox_textures = {
        "DOOM16RT.png", "DOOM16LF.png", "DOOM16UP.png", "DOOM16DN.png", "DOOM16FT.png", "DOOM16BK.png"
    };

    cubeMapTexture = std::make_unique<Texture>();

    int width = 0, height = 0, bit_depth = 0;
    std::vector<unsigned char*> face_data(6);
    vk::DeviceSize layerSize = 0;
    vk::DeviceSize imageSize = 0;

    for (size_t i = 0; i < 6; i++) {
        std::string path = skybox_base_dir.str() + skybox_textures[i];
        face_data[i] = stbi_load(path.c_str(), &width, &height, &bit_depth, 4);
        if (!face_data[i]) {
            spdlog::error("Failed to load skybox texture: {}", path);
            for (size_t j = 0; j < i; j++) { stbi_image_free(face_data[j]); }
            return;
        }
        layerSize = static_cast<vk::DeviceSize>(width) * static_cast<vk::DeviceSize>(height) * 4;
        imageSize += layerSize;
    }

    cubeMapTexture->createImage(device, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, vk::Format::eR8G8B8A8Unorm, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, 6, vk::ImageCreateFlagBits::eCubeCompatible);

    VulkanBuffer stagingBuffer;
    stagingBuffer.create(device, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* mappedData = device->getLogicalDevice().mapMemory(stagingBuffer.getBufferMemory(), 0, imageSize).value;
    for (size_t i = 0; i < 6; i++) {
        void* layerOffset = static_cast<char*>(mappedData) + i * layerSize;
        std::memcpy(layerOffset, face_data[i], static_cast<size_t>(layerSize));
    }
    device->getLogicalDevice().unmapMemory(stagingBuffer.getBufferMemory());

    for (size_t i = 0; i < 6; i++) { stbi_image_free(face_data[i]); }

    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);

    vk::ImageMemoryBarrier barrier{};
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image = cubeMapTexture->getImage();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;

    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcAccessMask = vk::AccessFlags{};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{}, {}, {}, barrier);

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = static_cast<uint32_t>(width);
    region.bufferImageHeight = static_cast<uint32_t>(height);
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 6;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    commandBuffer.copyBufferToImage(stagingBuffer.getBuffer(), cubeMapTexture->getImage(), vk::ImageLayout::eTransferDstOptimal, 1, &region);

    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{}, {}, {}, barrier);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), commandBuffer);

    stagingBuffer.cleanUp();

    cubeMapTexture->createImageView(device, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::eCube, 6);
    cubeMapTexture->createTextureSampler(device);

    createDescriptorSetForCubeMap();
}

void SkyBox::createDescriptorSetForCubeMap()
{
    vk::DescriptorSetLayoutBinding cubemapBinding{};
    cubemapBinding.binding = 1;
    cubemapBinding.descriptorCount = 1;
    cubemapBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    cubemapBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &cubemapBinding;

    auto layoutResult = device->getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    ASSERT_VULKAN(VkResult(layoutResult.result), "Failed to create skybox descriptor set layout!");
    descriptorSetLayout = layoutResult.value;

    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    auto poolResult = device->getLogicalDevice().createDescriptorPool(poolInfo);
    ASSERT_VULKAN(VkResult(poolResult.result), "Failed to create skybox descriptor pool!");
    descriptorPool = poolResult.value;

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    auto allocResult = device->getLogicalDevice().allocateDescriptorSets(allocInfo);
    ASSERT_VULKAN(VkResult(allocResult.result), "Failed to allocate skybox descriptor set!");
    descriptorSet = allocResult.value[0];

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = cubeMapTexture->getImageView();
    imageInfo.sampler = cubeMapTexture->getSampler();

    vk::WriteDescriptorSet write{};
    write.dstSet = descriptorSet;
    write.dstBinding = 1;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    device->getLogicalDevice().updateDescriptorSets(1, &write, 0, nullptr);
}

void SkyBox::createRenderPass(vk::Format format)
{
    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = format;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    auto result = device->getLogicalDevice().createRenderPass(renderPassInfo);
    ASSERT_VULKAN(VkResult(result.result), "Failed to create skybox render pass!");
    renderPass = result.value;
}

void SkyBox::createFramebuffers(size_t count, const std::vector<vk::ImageView>& imageViews, uint32_t width, uint32_t height)
{
    framebufferWidth = width;
    framebufferHeight = height;
    framebuffers.resize(count);
    for (size_t i = 0; i < count; i++) {
        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &imageViews[i];
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;
        auto fbResult = device->getLogicalDevice().createFramebuffer(fbInfo);
        ASSERT_VULKAN(VkResult(fbResult.result), "Failed to create skybox framebuffer!");
        framebuffers[i] = fbResult.value;
    }
}

void SkyBox::createGraphicsPipeline(vk::DescriptorSetLayout sharedLayout)
{
    std::stringstream skybox_shader_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    skybox_shader_dir << cwd.string() << RELATIVE_RESOURCE_PATH << "Shaders/skybox/";

    ShaderHelper shaderHelper;
    shaderHelper.compileShader(skybox_shader_dir.str(), "SkyBox.vert");
    shaderHelper.compileShader(skybox_shader_dir.str(), "SkyBox.frag");

    File vertexFile(shaderHelper.getShaderSpvDir(skybox_shader_dir.str(), "SkyBox.vert"));
    File fragmentFile(shaderHelper.getShaderSpvDir(skybox_shader_dir.str(), "SkyBox.frag"));
    std::vector<char> const vertexShaderCode = vertexFile.readCharSequence();
    std::vector<char> const fragmentShaderCode = fragmentFile.readCharSequence();

    vk::ShaderModule vertexShaderModule = shaderHelper.createShaderModule(device, vertexShaderCode);
    vk::ShaderModule fragmentShaderModule = shaderHelper.createShaderModule(device, fragmentShaderCode);

    vk::PipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertStageInfo.module = vertexShaderModule;
    vertStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragStageInfo.module = fragmentShaderModule;
    fragStageInfo.pName = "main";

    std::array skyStages = {vertStageInfo, fragStageInfo};

    vk::VertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 4> attributeDescriptions = vertex::getVertexInputAttributeDesc();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    std::array<vk::DescriptorSetLayout, 2> combinedLayouts = {sharedLayout, descriptorSetLayout};
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = combinedLayouts.data();

    auto layoutRes = device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    ASSERT_VULKAN(VkResult(layoutRes.result), "Failed to create skybox pipeline layout!");
    pipelineLayout = layoutRes.value;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = static_cast<uint32_t>(skyStages.size());
    pipelineInfo.pStages = skyStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    auto pipelineRes = device->getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
    ASSERT_VULKAN(VkResult(pipelineRes.result), "Failed to create skybox graphics pipeline!");
    graphicsPipeline = pipelineRes.value;

    device->getLogicalDevice().destroyShaderModule(vertexShaderModule);
    device->getLogicalDevice().destroyShaderModule(fragmentShaderModule);
}

void SkyBox::createMesh(vk::CommandPool commandPool)
{
    std::vector<unsigned int> indices = {
        0, 1, 2, 2, 1, 3, // front
        2, 3, 5, 5, 3, 7, // right
        5, 7, 4, 4, 7, 6, // back
        4, 6, 0, 0, 6, 1, // left
        4, 0, 5, 5, 0, 2, // top
        1, 6, 3, 3, 6, 7  // bottom
    };

    std::vector<Vertex> vertices = {
        Vertex(glm::vec3(-1.0F,  1.0F, -1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0)),
        Vertex(glm::vec3(-1.0F, -1.0F, -1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0)),
        Vertex(glm::vec3( 1.0F,  1.0F, -1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0)),
        Vertex(glm::vec3( 1.0F, -1.0F, -1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0)),
        Vertex(glm::vec3(-1.0F,  1.0F,  1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0)),
        Vertex(glm::vec3( 1.0F,  1.0F,  1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0)),
        Vertex(glm::vec3(-1.0F, -1.0F,  1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0)),
        Vertex(glm::vec3( 1.0F, -1.0F,  1.0F), glm::vec3(0), glm::vec3(0), glm::vec2(0))
    };

    skyMesh = std::make_unique<Kataglyphis::Mesh>();
    std::vector<unsigned int> materialIndex = {0};
    std::vector<ObjMaterial> materials = {ObjMaterial{}};
    skyMesh = std::make_unique<Mesh>(device, device->getGraphicsQueue(), commandPool, vertices, indices, materialIndex, materials);
}

void SkyBox::recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, const std::vector<vk::DescriptorSet> &descriptorSets)
{
    if (image_index >= framebuffers.size() || framebuffers.empty()) {
        spdlog::error("SkyBox: framebuffer not created or index out of range!");
        return;
    }

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[image_index];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = vk::Extent2D{framebufferWidth, framebufferHeight};

    vk::ClearValue clearValue{};
    clearValue.color = vk::ClearColorValue{std::array<float, 4>{0, 0, 0, 1}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

    std::vector<vk::DescriptorSet> skyboxDescriptorSets = {descriptorSets[0], descriptorSet};
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, skyboxDescriptorSets, nullptr);

    std::vector<vk::Buffer> const vertex_buffers = { skyMesh->getVertexBuffer() };
    vk::DeviceSize offsets[] = { 0 };
    commandBuffer.bindVertexBuffers(0, vertex_buffers, offsets);
    commandBuffer.bindIndexBuffer(skyMesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

    commandBuffer.drawIndexed(skyMesh->getIndexCount(), 1, 0, 0, 0);
    commandBuffer.endRenderPass();
}

void SkyBox::cleanUp()
{
    if (device) {
        for (auto fb : framebuffers) {
            device->getLogicalDevice().destroyFramebuffer(fb);
        }
        framebuffers.clear();
        if (graphicsPipeline) { device->getLogicalDevice().destroyPipeline(graphicsPipeline); }
        if (pipelineLayout) { device->getLogicalDevice().destroyPipelineLayout(pipelineLayout); }
        if (descriptorSetLayout) { device->getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout); }
        if (descriptorPool) { device->getLogicalDevice().destroyDescriptorPool(descriptorPool); }
        if (renderPass) { device->getLogicalDevice().destroyRenderPass(renderPass); }
    }
    if (skyMesh) {
        skyMesh->cleanUp();
    }
    if (cubeMapTexture) {
        cubeMapTexture->cleanUp();
    }
}

SkyBox::~SkyBox() = default;

}