module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"

#include "common/FormatHelper.hpp"

#include "common/Utilities.hpp"

module kataglyphis.vulkan.rasterizer;

import kataglyphis.vulkan.file;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.image;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.shader_helper;

namespace {
auto hasStencilComponent(vk::Format format) -> bool
{
    return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
}
}// namespace

Kataglyphis::VulkanRendererInternals::Rasterizer::Rasterizer() = default;

void Kataglyphis::VulkanRendererInternals::Rasterizer::init(VulkanDevice *in_device,
  VulkanSwapChain *swap_chain,
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts,
  vk::CommandPool &commandPool)
{
    this->device = in_device;
    this->vulkanSwapChain = swap_chain;

    createTextures(commandPool);
    createRenderPass();
    createPushConstantRange();
    createGraphicsPipeline(descriptorSetLayouts);
    createFramebuffer();
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::shaderHotReload(
  const std::vector<vk::DescriptorSetLayout> &descriptor_set_layouts)
{
    device->getLogicalDevice().destroyPipeline(graphics_pipeline);
    createGraphicsPipeline(descriptor_set_layouts);
}

auto Kataglyphis::VulkanRendererInternals::Rasterizer::getOffscreenTexture(uint32_t index) -> Kataglyphis::Texture &
{
    return *offscreenTextures[index];
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::setPushConstant(PushConstantRasterizer push_constant)
{
    this->pushConstant = push_constant;
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::recordCommands(vk::CommandBuffer &commandBuffer,
  uint32_t image_index,
  Scene *scene,
  const std::vector<vk::DescriptorSet> &descriptorSets)
{
    vk::RenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.renderArea.offset = vk::Offset2D{ 0, 0 };
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    render_pass_begin_info.renderArea.extent = swap_chain_extent;

    std::array<vk::ClearValue, 2> clear_values = {};
    clear_values[0].color = vk::ClearColorValue{ std::array<float, 4>{ 0.2F, 0.65F, 0.4F, 1.0F } };
    clear_values[1].depthStencil = vk::ClearDepthStencilValue{ 1.0F, 0 };

    render_pass_begin_info.pClearValues = clear_values.data();
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_begin_info.framebuffer = framebuffer[image_index];

    commandBuffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);

    for (uint32_t m = 0; m < scene->getModelCount(); m++) {
        pushConstant.model = scene->getModelMatrix(0);
        commandBuffer.pushConstants(
          pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstantRasterizer), &pushConstant);

        for (unsigned int k = 0; k < scene->getMeshCount(m); k++) {
            std::vector<vk::Buffer> const vertex_buffers = { scene->getVertexBuffer(m, k) };
            vk::DeviceSize offsets[] = { 0 };
            commandBuffer.bindVertexBuffers(0, vertex_buffers, offsets);

            commandBuffer.bindIndexBuffer(scene->getIndexBuffer(m, k), 0, vk::IndexType::eUint32);

            commandBuffer.bindDescriptorSets(
              vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptorSets, nullptr);

            commandBuffer.drawIndexed(scene->getIndexCount(m, k), 1, 0, 0, 0);
        }
    }

    commandBuffer.endRenderPass();
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::cleanUp()
{
    for (auto &framebuffer_handle : framebuffer) { device->getLogicalDevice().destroyFramebuffer(framebuffer_handle); }

    for (const auto &texture : offscreenTextures) { texture->cleanUp(); }

    depthBufferImage->cleanUp();

    device->getLogicalDevice().destroyPipeline(graphics_pipeline);
    device->getLogicalDevice().destroyPipelineLayout(pipeline_layout);
    device->getLogicalDevice().destroyRenderPass(render_pass);
}

Kataglyphis::VulkanRendererInternals::Rasterizer::~Rasterizer() = default;

void Kataglyphis::VulkanRendererInternals::Rasterizer::createRenderPass()
{
    vk::AttachmentDescription color_attachment;
    constexpr vk::Format offscreen_format = vk::Format::eR8G8B8A8Unorm;
    color_attachment.format = offscreen_format;
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentDescription depth_attachment;
    depth_attachment.format = choose_supported_format(device->getPhysicalDevice(),
      { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint },
      vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    depth_attachment.samples = vk::SampleCountFlagBits::e1;
    depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
    depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference color_attachment_reference;
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depth_attachment_reference;
    depth_attachment_reference.attachment = 1;
    depth_attachment_reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_reference;
    subpass.pDepthStencilAttachment = &depth_attachment_reference;

    std::array<vk::SubpassDependency, 1> subpass_dependencies{};

    subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    subpass_dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    subpass_dependencies[0].srcAccessMask = vk::AccessFlags{};

    subpass_dependencies[0].dstSubpass = 0;
    subpass_dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    subpass_dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    subpass_dependencies[0].dependencyFlags = vk::DependencyFlags{};

    std::array<vk::AttachmentDescription, 2> render_pass_attachments = { color_attachment, depth_attachment };

    vk::RenderPassCreateInfo render_pass_create_info;
    render_pass_create_info.attachmentCount = static_cast<uint32_t>(render_pass_attachments.size());
    render_pass_create_info.pAttachments = render_pass_attachments.data();
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = static_cast<uint32_t>(subpass_dependencies.size());
    render_pass_create_info.pDependencies = subpass_dependencies.data();

    auto result = device->getLogicalDevice().createRenderPass(render_pass_create_info);
    if (result.result == vk::Result::eSuccess) {
        render_pass = result.value;
    } else {
        ASSERT_VULKAN(static_cast<VkResult>(result.result), "Failed to create render pass!")
    }
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createFramebuffer()
{
    framebuffer.resize(vulkanSwapChain->getNumberSwapChainImages());

    for (size_t i = 0; i < framebuffer.size(); i++) {
        std::array<vk::ImageView, 2> attachments = { offscreenTextures[i]->getImageView(),
            depthBufferImage->getImageView() };

        vk::FramebufferCreateInfo frame_buffer_create_info;
        frame_buffer_create_info.renderPass = render_pass;
        frame_buffer_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        frame_buffer_create_info.pAttachments = attachments.data();
        const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        frame_buffer_create_info.width = swap_chain_extent.width;
        frame_buffer_create_info.height = swap_chain_extent.height;
        frame_buffer_create_info.layers = 1;

        auto result = device->getLogicalDevice().createFramebuffer(frame_buffer_create_info);
        if (result.result == vk::Result::eSuccess) {
            framebuffer[i] = result.value;
        } else {
            ASSERT_VULKAN(static_cast<VkResult>(result.result), "Failed to create framebuffer!")
        }
    }
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createPushConstantRange()
{
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eVertex;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantRasterizer);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createTextures(vk::CommandPool &commandPool)
{
    offscreenTextures.resize(vulkanSwapChain->getNumberSwapChainImages());

    vk::CommandBuffer cmdBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(
      device->getLogicalDevice(), commandPool);

    for (uint32_t index = 0; index < vulkanSwapChain->getNumberSwapChainImages(); index++) {
        auto texture = std::make_unique<Texture>();
        const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        constexpr vk::Format offscreen_format = vk::Format::eR8G8B8A8Unorm;

        texture->createImage(device,
          swap_chain_extent.width,
          swap_chain_extent.height,
          1,
          offscreen_format,
          vk::ImageTiling::eOptimal,
          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage
            | vk::ImageUsageFlagBits::eTransferDst,
          vk::MemoryPropertyFlagBits::eDeviceLocal);

        texture->createImageView(device, offscreen_format, vk::ImageAspectFlagBits::eColor, 1);

        offscreenTextures[index] = std::move(texture);
    }

    vk::Format const depth_format = choose_supported_format(device->getPhysicalDevice(),
      { vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint },
      vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    depthBufferImage = std::make_unique<Texture>();
    depthBufferImage->createImage(device,
      swap_chain_extent.width,
      swap_chain_extent.height,
      1,
      depth_format,
      vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eDepthStencilAttachment,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::ImageAspectFlags depth_aspect_flags = vk::ImageAspectFlagBits::eDepth;
    if (hasStencilComponent(depth_format)) { depth_aspect_flags |= vk::ImageAspectFlagBits::eStencil; }

    depthBufferImage->createImageView(device, depth_format, depth_aspect_flags, 1);

    VulkanImage &vulkanImage = depthBufferImage->getVulkanImage();
    vulkanImage.transitionImageLayout(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      depth_aspect_flags,
      1);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), cmdBuffer);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createGraphicsPipeline(
  const std::vector<vk::DescriptorSetLayout> &descriptorSetLayouts)
{
    std::stringstream rasterizer_shader_dir;
    std::filesystem::path const cwd = std::filesystem::current_path();
    rasterizer_shader_dir << cwd.string();
    rasterizer_shader_dir << RELATIVE_RESOURCE_PATH;
    rasterizer_shader_dir << "Shaders/rasterizer/";

    ShaderHelper shaderHelper;
    shaderHelper.compileShader(rasterizer_shader_dir.str(), "shader.vert");
    shaderHelper.compileShader(rasterizer_shader_dir.str(), "shader.frag");

    File vertexFile(shaderHelper.getShaderSpvDir(rasterizer_shader_dir.str(), "shader.vert"));
    File fragmentFile(shaderHelper.getShaderSpvDir(rasterizer_shader_dir.str(), "shader.frag"));
    std::vector<char> const vertex_shader_code = vertexFile.readCharSequence();
    std::vector<char> const fragment_shader_code = fragmentFile.readCharSequence();

    vk::ShaderModule vertex_shader_module = shaderHelper.createShaderModule(device, vertex_shader_code);
    vk::ShaderModule fragment_shader_module = shaderHelper.createShaderModule(device, fragment_shader_code);

    vk::PipelineShaderStageCreateInfo vertex_shader_create_info;
    vertex_shader_create_info.stage = vk::ShaderStageFlagBits::eVertex;
    vertex_shader_create_info.module = vertex_shader_module;
    vertex_shader_create_info.pName = "main";

    vk::PipelineShaderStageCreateInfo fragment_shader_create_info;
    fragment_shader_create_info.stage = vk::ShaderStageFlagBits::eFragment;
    fragment_shader_create_info.module = fragment_shader_module;
    fragment_shader_create_info.pName = "main";

    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = { vertex_shader_create_info,
        fragment_shader_create_info };

    vk::VertexInputBindingDescription binding_description;
    binding_description.binding = 0;
    binding_description.stride = sizeof(Vertex);
    binding_description.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 4> attribute_describtions = vertex::getVertexInputAttributeDesc();

    vk::PipelineVertexInputStateCreateInfo vertex_input_create_info;
    vertex_input_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_create_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_create_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_describtions.size());
    vertex_input_create_info.pVertexAttributeDescriptions = attribute_describtions.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    vk::Viewport viewport;
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    const vk::Extent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    viewport.width = static_cast<float>(swap_chain_extent.width);
    viewport.height = static_cast<float>(swap_chain_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;

    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D{ 0, 0 };
    scissor.extent = swap_chain_extent;

    vk::PipelineViewportStateCreateInfo viewport_state_create_info;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports = &viewport;
    viewport_state_create_info.scissorCount = 1;
    viewport_state_create_info.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterizer_create_info;
    rasterizer_create_info.depthClampEnable = VK_FALSE;
    rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_create_info.polygonMode = vk::PolygonMode::eFill;
    rasterizer_create_info.lineWidth = 1.0F;
    rasterizer_create_info.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer_create_info.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer_create_info.depthBiasClamp = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisample_create_info;
    multisample_create_info.sampleShadingEnable = VK_FALSE;
    multisample_create_info.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState color_state;
    color_state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                                 | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    color_state.blendEnable = VK_TRUE;
    color_state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    color_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    color_state.colorBlendOp = vk::BlendOp::eAdd;
    color_state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    color_state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    color_state.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo color_blending_create_info;
    color_blending_create_info.logicOpEnable = VK_FALSE;
    color_blending_create_info.attachmentCount = 1;
    color_blending_create_info.pAttachments = &color_state;

    vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    auto layout_result = device->getLogicalDevice().createPipelineLayout(pipeline_layout_create_info);
    if (layout_result.result == vk::Result::eSuccess) {
        pipeline_layout = layout_result.value;
    } else {
        ASSERT_VULKAN(static_cast<VkResult>(layout_result.result), "Failed to create pipeline layout!")
    }

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_create_info;
    depth_stencil_create_info.depthTestEnable = VK_TRUE;
    depth_stencil_create_info.depthWriteEnable = VK_TRUE;
    depth_stencil_create_info.depthCompareOp = vk::CompareOp::eLess;
    depth_stencil_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_create_info.stencilTestEnable = VK_FALSE;

    vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info;
    graphics_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    graphics_pipeline_create_info.pStages = shader_stages.data();
    graphics_pipeline_create_info.pVertexInputState = &vertex_input_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &input_assembly;
    graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
    graphics_pipeline_create_info.pDynamicState = nullptr;
    graphics_pipeline_create_info.pRasterizationState = &rasterizer_create_info;
    graphics_pipeline_create_info.pMultisampleState = &multisample_create_info;
    graphics_pipeline_create_info.pColorBlendState = &color_blending_create_info;
    graphics_pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
    graphics_pipeline_create_info.layout = pipeline_layout;
    graphics_pipeline_create_info.renderPass = render_pass;
    graphics_pipeline_create_info.subpass = 0;
    graphics_pipeline_create_info.basePipelineHandle = nullptr;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    auto pipeline_result = device->getLogicalDevice().createGraphicsPipelines(nullptr, graphics_pipeline_create_info);
    if (pipeline_result.result == vk::Result::eSuccess) {
        graphics_pipeline = pipeline_result.value.front();
    } else {
        ASSERT_VULKAN(static_cast<VkResult>(pipeline_result.result), "Failed to create a graphics pipeline!")
    }

    device->getLogicalDevice().destroyShaderModule(vertex_shader_module);
    device->getLogicalDevice().destroyShaderModule(fragment_shader_module);
}