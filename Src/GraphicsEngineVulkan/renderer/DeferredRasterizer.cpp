module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include "common/FormatHelper.hpp"
#include "common/Utilities.hpp"

module kataglyphis.vulkan.deferred_rasterizer;

import kataglyphis.vulkan.file;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.shader_helper;

using namespace Kataglyphis::VulkanRendererInternals;

DeferredRasterizer::DeferredRasterizer() = default;

void DeferredRasterizer::init(VulkanDevice *in_device,
  VulkanSwapChain *swap_chain,
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts,
  vk::CommandPool &commandPool)
{
    device = in_device;
    vulkanSwapChain = swap_chain;

    commandBufferManager = CommandBufferManager();

    createTextures(commandPool);
    createRenderPass();
    createPushConstantRange();
    createPipelines(descriptorSetLayouts);
    createFramebuffer();
}

void DeferredRasterizer::shaderHotReload(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts)
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

void DeferredRasterizer::createTextures(vk::CommandPool &commandPool)
{
    uint32_t count = vulkanSwapChain->getNumberSwapChainImages();
    offscreenTextures.resize(count);
    gBufferPositions.resize(count);
    gBufferNormals.resize(count);
    gBufferAlbedos.resize(count);
    gBufferMaterials.resize(count);

    vk::CommandBuffer cmdBuffer = CommandBufferManager::beginCommandBuffer(device->getLogicalDevice(), commandPool);

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
    createAttachment(offscreenTextures, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst);
    createAttachment(gBufferPositions, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);
    createAttachment(gBufferNormals, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);
    createAttachment(gBufferAlbedos, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);
    createAttachment(gBufferMaterials, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);

    // Depth buffer
    depthBufferImage = std::make_unique<Texture>();
    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    depthBufferImage->createImage(device, extent.width, extent.height, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthBufferImage->createImageView(device, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);

    CommandBufferManager::endAndSubmitCommandBuffer(device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), cmdBuffer);
}





void DeferredRasterizer::createPushConstantRange()
{
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eAll;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantRasterizer);
}


void DeferredRasterizer::cleanUp()
{
    auto logicalDevice = device->getLogicalDevice();
    logicalDevice.destroyPipeline(geometryPipeline);
    logicalDevice.destroyPipelineLayout(geometryPipelineLayout);
    logicalDevice.destroyPipeline(lightingPipeline);
    logicalDevice.destroyPipelineLayout(lightingPipelineLayout);
    logicalDevice.destroyRenderPass(renderPass);

    for (auto &fb : framebuffer) {
        logicalDevice.destroyFramebuffer(fb);
    }

    offscreenTextures.clear();
    gBufferPositions.clear();
    gBufferNormals.clear();
    gBufferAlbedos.clear();
    gBufferMaterials.clear();
    depthBufferImage.reset();
}

DeferredRasterizer::~DeferredRasterizer() = default;

void DeferredRasterizer::createRenderPass()
{
    // Attachments
    // 0: Final Color (Offscreen)
    // 1: Position
    // 2: Normal
    // 3: Albedo
    // 4: Material
    // 5: Depth

    vk::Format finalFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format positionFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format normalFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format albedoFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format materialFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format depthFormat = Kataglyphis::choose_supported_format(device->getPhysicalDevice(), { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    auto createAttachmentDesc = [](vk::Format format, vk::ImageLayout finalLayout, bool isDepth = false) {
        vk::AttachmentDescription desc;
        desc.format = format;
        desc.samples = vk::SampleCountFlagBits::e1;
        desc.loadOp = vk::AttachmentLoadOp::eClear;
        desc.storeOp = vk::AttachmentStoreOp::eStore;
        desc.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        desc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        desc.initialLayout = vk::ImageLayout::eUndefined;
        desc.finalLayout = finalLayout;
        return desc;
    };

    std::array<vk::AttachmentDescription, 6> attachments = {
        createAttachmentDesc(finalFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 0: Final Output
        createAttachmentDesc(positionFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 1: Position
        createAttachmentDesc(normalFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 2: Normal
        createAttachmentDesc(albedoFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 3: Albedo
        createAttachmentDesc(materialFormat, vk::ImageLayout::eShaderReadOnlyOptimal), // 4: Material
        createAttachmentDesc(depthFormat, vk::ImageLayout::eDepthStencilAttachmentOptimal, true) // 5: Depth
    };

    // Subpass 0: Geometry Pass
    std::array<vk::AttachmentReference, 4> geometryColorRefs = {
        vk::AttachmentReference{1, vk::ImageLayout::eColorAttachmentOptimal}, // Position
        vk::AttachmentReference{2, vk::ImageLayout::eColorAttachmentOptimal}, // Normal
        vk::AttachmentReference{3, vk::ImageLayout::eColorAttachmentOptimal}, // Albedo
        vk::AttachmentReference{4, vk::ImageLayout::eColorAttachmentOptimal}  // Material
    };
    vk::AttachmentReference geometryDepthRef{5, vk::ImageLayout::eDepthStencilAttachmentOptimal};

    vk::SubpassDescription geometrySubpass;
    geometrySubpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    geometrySubpass.colorAttachmentCount = static_cast<uint32_t>(geometryColorRefs.size());
    geometrySubpass.pColorAttachments = geometryColorRefs.data();
    geometrySubpass.pDepthStencilAttachment = &geometryDepthRef;

    // Subpass 1: Lighting Pass
    vk::AttachmentReference lightingColorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
    
    std::array<vk::AttachmentReference, 5> lightingInputRefs = {
        vk::AttachmentReference{1, vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::AttachmentReference{2, vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::AttachmentReference{3, vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::AttachmentReference{4, vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::AttachmentReference{5, vk::ImageLayout::eShaderReadOnlyOptimal}
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

void DeferredRasterizer::createPipelines(const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    // Basic setup for geometry pipeline
    std::filesystem::path cwd = std::filesystem::current_path();
    std::stringstream shader_dir;
    shader_dir << cwd.string() << RELATIVE_RESOURCE_PATH << "Shaders/deferred/";

    ShaderHelper shaderHelper;
    shaderHelper.compileShader(shader_dir.str(), "geometry.vert");
    shaderHelper.compileShader(shader_dir.str(), "geometry.frag");
    shaderHelper.compileShader(shader_dir.str(), "lighting.vert");
    shaderHelper.compileShader(shader_dir.str(), "lighting.frag");

    // Implement actual pipeline creation here...
    File geomVertFile(shaderHelper.getShaderSpvDir(shader_dir.str(), "geometry.vert"));
    File geomFragFile(shaderHelper.getShaderSpvDir(shader_dir.str(), "geometry.frag"));
    vk::ShaderModule geomVertModule = shaderHelper.createShaderModule(device, geomVertFile.readCharSequence());
    vk::ShaderModule geomFragModule = shaderHelper.createShaderModule(device, geomFragFile.readCharSequence());

    vk::PipelineShaderStageCreateInfo geomVertStage{ {}, vk::ShaderStageFlagBits::eVertex, geomVertModule, "main" };
    vk::PipelineShaderStageCreateInfo geomFragStage{ {}, vk::ShaderStageFlagBits::eFragment, geomFragModule, "main" };
    std::array<vk::PipelineShaderStageCreateInfo, 2> geomStages = { geomVertStage, geomFragStage };

    vk::VertexInputBindingDescription bindingDescription;
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

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

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

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = vk::CompareOp::eLess;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    std::array<vk::PipelineColorBlendAttachmentState, 4> colorBlendAttachments = {
        colorBlendAttachment, colorBlendAttachment, colorBlendAttachment, colorBlendAttachment
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push_constant_range;

    geometryPipelineLayout = device->getLogicalDevice().createPipelineLayout(pipelineLayoutInfo).value;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = geomStages.size();
    pipelineInfo.pStages = geomStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = geometryPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    geometryPipeline = device->getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

    device->getLogicalDevice().destroyShaderModule(geomVertModule);
    device->getLogicalDevice().destroyShaderModule(geomFragModule);

    // Lighting Pipeline
    File lightVertFile(shaderHelper.getShaderSpvDir(shader_dir.str(), "lighting.vert"));
    File lightFragFile(shaderHelper.getShaderSpvDir(shader_dir.str(), "lighting.frag"));
    vk::ShaderModule lightVertModule = shaderHelper.createShaderModule(device, lightVertFile.readCharSequence());
    vk::ShaderModule lightFragModule = shaderHelper.createShaderModule(device, lightFragFile.readCharSequence());

    vk::PipelineShaderStageCreateInfo lightVertStage{ {}, vk::ShaderStageFlagBits::eVertex, lightVertModule, "main" };
    vk::PipelineShaderStageCreateInfo lightFragStage{ {}, vk::ShaderStageFlagBits::eFragment, lightFragModule, "main" };
    std::array<vk::PipelineShaderStageCreateInfo, 2> lightStages = { lightVertStage, lightFragStage };

    vk::PipelineVertexInputStateCreateInfo emptyVertexInputInfo{};

    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo lightColorBlending{};
    lightColorBlending.logicOpEnable = VK_FALSE;
    lightColorBlending.attachmentCount = 1;
    lightColorBlending.pAttachments = &colorBlendAttachment;

    vk::PipelineLayoutCreateInfo lightPipelineLayoutInfo{};
    lightPipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    lightPipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    // No push constants for lighting pass usually, or same if needed

    lightingPipelineLayout = device->getLogicalDevice().createPipelineLayout(lightPipelineLayoutInfo).value;

    pipelineInfo.stageCount = lightStages.size();
    pipelineInfo.pStages = lightStages.data();
    pipelineInfo.pVertexInputState = &emptyVertexInputInfo;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &lightColorBlending;
    pipelineInfo.layout = lightingPipelineLayout;
    pipelineInfo.subpass = 1;

    lightingPipeline = device->getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

    device->getLogicalDevice().destroyShaderModule(lightVertModule);
    device->getLogicalDevice().destroyShaderModule(lightFragModule);
}

void DeferredRasterizer::createFramebuffer()
{
    std::array<vk::ImageView, 6> attachments;
    const vk::Extent2D &extent = vulkanSwapChain->getSwapChainExtent();

    vk::FramebufferCreateInfo framebufferInfo;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    framebuffer.resize(vulkanSwapChain->getNumberSwapChainImages());
    for (uint32_t i = 0; i < vulkanSwapChain->getNumberSwapChainImages(); i++) {
        attachments[0] = offscreenTextures[i]->getImageView();
        attachments[1] = gBufferPositions[i]->getImageView();
        attachments[2] = gBufferNormals[i]->getImageView();
        attachments[3] = gBufferAlbedos[i]->getImageView();
        attachments[4] = gBufferMaterials[i]->getImageView();
        attachments[5] = depthBufferImage->getImageView();

        auto result = device->getLogicalDevice().createFramebuffer(framebufferInfo);
        ASSERT_VULKAN(VkResult(result.result), "Failed to create deferred framebuffer!");
        framebuffer[i] = result.value;
    }
}


void DeferredRasterizer::recordCommands(vk::CommandBuffer &commandBuffer, uint32_t image_index, Kataglyphis::Scene *scene, const std::vector<vk::DescriptorSet> &descriptorSets)
{
    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer[image_index];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapChain->getSwapChainExtent();

    std::array<vk::ClearValue, 6> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[2].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    clearValues[3].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[4].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[5].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Subpass 0: Geometry
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, geometryPipeline);

    for (uint32_t m = 0; m < scene->getModelCount(); m++) {
        pushConstant.model = scene->getModelMatrix(0); // Match Rasterizer hardcoded index
        commandBuffer.pushConstants(
          geometryPipelineLayout, vk::ShaderStageFlagBits::eAll, 0, sizeof(PushConstantRasterizer), &pushConstant);

        for (unsigned int k = 0; k < scene->getMeshCount(m); k++) {
            std::vector<vk::Buffer> const vertex_buffers = { scene->getVertexBuffer(m, k) };
            vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, vertex_buffers, offsets);

            commandBuffer.bindIndexBuffer(scene->getIndexBuffer(m, k), 0, vk::IndexType::eUint32);

            // Bind global descriptor set (set 0)
            commandBuffer.bindDescriptorSets(
              vk::PipelineBindPoint::eGraphics, geometryPipelineLayout, 0, 1, &descriptorSets[0], 0, nullptr);

            commandBuffer.drawIndexed(scene->getIndexCount(m, k), 1, 0, 0, 0);
        }
    }

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
