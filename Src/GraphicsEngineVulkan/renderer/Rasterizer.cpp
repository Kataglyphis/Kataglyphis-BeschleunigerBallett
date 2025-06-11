#include "Rasterizer.hpp"

#include <array>
#include <filesystem>
#include <vector>

#include "common/FormatHelper.hpp"
#include "scene/Vertex.hpp"
#include "util/File.hpp"
#include "vulkan_base/ShaderHelper.hpp"

#include "common/Utilities.hpp"
#include "renderer/VulkanRendererConfig.hpp"

Kataglyphis::VulkanRendererInternals::Rasterizer::Rasterizer() {}

void Kataglyphis::VulkanRendererInternals::Rasterizer::init(VulkanDevice *device,
  VulkanSwapChain *vulkanSwapChain,
  const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts,
  VkCommandPool &commandPool)
{
    this->device = device;
    this->vulkanSwapChain = vulkanSwapChain;

    createTextures(commandPool);
    createRenderPass();
    createPushConstantRange();
    createGraphicsPipeline(descriptorSetLayouts);
    createFramebuffer();
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::shaderHotReload(
  const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts)
{
    vkDestroyPipeline(device->getLogicalDevice(), graphics_pipeline, nullptr);
    createGraphicsPipeline(descriptor_set_layouts);
}

Kataglyphis::Texture &Kataglyphis::VulkanRendererInternals::Rasterizer::getOffscreenTexture(uint32_t index)
{
    return offscreenTextures[index];
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::setPushConstant(PushConstantRasterizer pushConstant)
{
    this->pushConstant = pushConstant;
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::recordCommands(VkCommandBuffer &commandBuffer,
  uint32_t image_index,
  Scene *scene,
  const std::vector<VkDescriptorSet> &descriptorSets)
{
    // information about how to begin a render pass (only needed for graphical
    // applications)
    VkRenderPassBeginInfo render_pass_begin_info{};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    render_pass_begin_info.renderArea.extent = swap_chain_extent;

    // make sure the order you put the values into the array matches with the
    // attchment order you have defined previous
    std::array<VkClearValue, 2> clear_values = {};
    clear_values[0].color = { 0.2f, 0.65f, 0.4f, 1.0f };
    clear_values[1].depthStencil = { 1.0f, 0 };

    render_pass_begin_info.pClearValues = clear_values.data();
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_begin_info.framebuffer = framebuffer[image_index];

    // begin render pass
    vkCmdBeginRenderPass(commandBuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    // bind pipeline to be used in render pass
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

    for (uint32_t m = 0; m < static_cast<uint32_t>(scene->getModelCount()); m++) {
        // for GCC doen't allow references on rvalues go like that ...
        pushConstant.model = scene->getModelMatrix(0);
        // just "Push" constants to given shader stage directly (no buffer)
        vkCmdPushConstants(commandBuffer,
          pipeline_layout,
          VK_SHADER_STAGE_VERTEX_BIT,// stage to push constants to
          0,// offset to push constants to update
          sizeof(PushConstantRasterizer),// size of data being pushed
          &pushConstant);// using model of current mesh (can be array)

        for (unsigned int k = 0; k < scene->getMeshCount(m); k++) {
            // list of vertex buffers we want to draw
            VkBuffer vertex_buffers[] = { scene->getVertexBuffer(m, k) };// buffers to bind
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer,
              0,
              1,
              vertex_buffers,
              offsets);// command to bind vertex buffer before drawing with them

            // bind mesh index buffer with 0 offset and using the uint32 type
            vkCmdBindIndexBuffer(commandBuffer, scene->getIndexBuffer(m, k), 0, VK_INDEX_TYPE_UINT32);

            // bind descriptor sets
            vkCmdBindDescriptorSets(commandBuffer,
              VK_PIPELINE_BIND_POINT_GRAPHICS,
              pipeline_layout,
              0,
              static_cast<uint32_t>(descriptorSets.size()),
              descriptorSets.data(),
              0,
              nullptr);

            // execute pipeline
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(scene->getIndexCount(m, k)), 1, 0, 0, 0);
        }
    }

    // end render pass
    vkCmdEndRenderPass(commandBuffer);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::cleanUp()
{
    for (auto framebuffer : framebuffer) { vkDestroyFramebuffer(device->getLogicalDevice(), framebuffer, nullptr); }

    for (Texture texture : offscreenTextures) { texture.cleanUp(); }

    depthBufferImage.cleanUp();

    vkDestroyPipeline(device->getLogicalDevice(), graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(device->getLogicalDevice(), pipeline_layout, nullptr);
    vkDestroyRenderPass(device->getLogicalDevice(), render_pass, nullptr);
}

Kataglyphis::VulkanRendererInternals::Rasterizer::~Rasterizer() {}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createRenderPass()
{
    // Color attachment of render pass
    VkAttachmentDescription color_attachment{};
    const VkFormat &swap_chain_image_format = vulkanSwapChain->getSwapChainFormat();
    color_attachment.format = swap_chain_image_format;// format to use for attachment
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;// number of samples to write for multisampling
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;// describes what to do with attachment
                                                          // before rendering
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;// describes what to do with attachment
                                                            // after rendering
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;// describes what to do with stencil
                                                                     // before rendering
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;// describes what to do with stencil
                                                                       // after rendering

    // framebuffer data will be stored as an image, but images can be given
    // different layouts to give optimal use for certain operations
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;// image data layout before render pass starts
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;// image data layout after render pass (to
                                                           // change to)

    // depth attachment of render pass
    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = choose_supported_format(device->getPhysicalDevice(),
      { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT },
      VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // attachment reference uses an attachment index that refers to index in the
    // attachment list passed to renderPassCreateInfo
    VkAttachmentReference color_attachment_reference{};
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // attachment reference
    VkAttachmentReference depth_attachment_reference{};
    depth_attachment_reference.attachment = 1;
    depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // information about a particular subpass the render pass is using
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;// pipeline type subpass is to be bound
                                                                // to
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_reference;
    subpass.pDepthStencilAttachment = &depth_attachment_reference;

    // need to determine when layout transitions occur using subpass dependencies
    std::array<VkSubpassDependency, 1> subpass_dependencies;

    // conversion from VK_IMAGE_LAYOUT_UNDEFINED to
    // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL transition must happen after ....
    subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;// subpass index (VK_SUBPASS_EXTERNAL = Special
                                                             // value meaning outside of renderpass)
    subpass_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;// pipeline stage
    subpass_dependencies[0].srcAccessMask = 0;// stage access mask (memory access)

    // but must happen before ...
    subpass_dependencies[0].dstSubpass = 0;
    subpass_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpass_dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    subpass_dependencies[0].dependencyFlags = 0;// VK_DEPENDENCY_BY_REGION_BIT;

    std::array<VkAttachmentDescription, 2> render_pass_attachments = { color_attachment, depth_attachment };

    // create info for render pass
    VkRenderPassCreateInfo render_pass_create_info{};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.attachmentCount = static_cast<uint32_t>(render_pass_attachments.size());
    render_pass_create_info.pAttachments = render_pass_attachments.data();
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    render_pass_create_info.dependencyCount = static_cast<uint32_t>(subpass_dependencies.size());
    render_pass_create_info.pDependencies = subpass_dependencies.data();

    VkResult result = vkCreateRenderPass(device->getLogicalDevice(), &render_pass_create_info, nullptr, &render_pass);
    ASSERT_VULKAN(result, "Failed to create render pass!")
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createFramebuffer()
{
    framebuffer.resize(vulkanSwapChain->getNumberSwapChainImages());

    for (size_t i = 0; i < framebuffer.size(); i++) {
        std::array<VkImageView, 2> attachments = { offscreenTextures[i].getImageView(),
            depthBufferImage.getImageView() };

        VkFramebufferCreateInfo frame_buffer_create_info{};
        frame_buffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frame_buffer_create_info.renderPass = render_pass;
        frame_buffer_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        frame_buffer_create_info.pAttachments = attachments.data();
        const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        frame_buffer_create_info.width = swap_chain_extent.width;
        frame_buffer_create_info.height = swap_chain_extent.height;
        frame_buffer_create_info.layers = 1;

        VkResult result =
          vkCreateFramebuffer(device->getLogicalDevice(), &frame_buffer_create_info, nullptr, &framebuffer[i]);
        ASSERT_VULKAN(result, "Failed to create framebuffer!");
    }
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createPushConstantRange()
{
    // define push constant values (no 'create' needed)
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantRasterizer);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createTextures(VkCommandPool &commandPool)
{
    offscreenTextures.resize(vulkanSwapChain->getNumberSwapChainImages());

    VkCommandBuffer cmdBuffer = commandBufferManager.beginCommandBuffer(device->getLogicalDevice(), commandPool);

    for (uint32_t index = 0; index < static_cast<uint32_t>(vulkanSwapChain->getNumberSwapChainImages()); index++) {
        Texture texture{};
        const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        const VkFormat &swap_chain_image_format = vulkanSwapChain->getSwapChainFormat();

        texture.createImage(device,
          swap_chain_extent.width,
          swap_chain_extent.height,
          1,
          swap_chain_image_format,
          VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        texture.createImageView(device, swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        // --- WE NEED A DIFFERENT LAYOUT FOR USAGE
        VulkanImage &image = texture.getVulkanImage();
        image.transitionImageLayout(
          cmdBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, VK_IMAGE_ASPECT_COLOR_BIT);

        offscreenTextures[index] = texture;
    }

    VkFormat depth_format = choose_supported_format(device->getPhysicalDevice(),
      { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT },
      VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    // create depth buffer image
    // MIP LEVELS: for depth texture we only want 1 level :)
    const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    depthBufferImage.createImage(device,
      swap_chain_extent.width,
      swap_chain_extent.height,
      1,
      depth_format,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // depth buffer image view
    // MIP LEVELS: for depth texture we only want 1 level :)
    depthBufferImage.createImageView(device, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1);

    // --- WE NEED A DIFFERENT LAYOUT FOR USAGE
    VulkanImage &vulkanImage = depthBufferImage.getVulkanImage();
    vulkanImage.transitionImageLayout(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      commandPool,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
      1);

    commandBufferManager.endAndSubmitCommandBuffer(
      device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), cmdBuffer);
}

void Kataglyphis::VulkanRendererInternals::Rasterizer::createGraphicsPipeline(
  const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
{
    std::stringstream rasterizer_shader_dir;
    std::filesystem::path cwd = std::filesystem::current_path();
    rasterizer_shader_dir << cwd.string();
    rasterizer_shader_dir << RELATIVE_RESOURCE_PATH;
    rasterizer_shader_dir << "Shaders/rasterizer/";

    ShaderHelper shaderHelper;
    shaderHelper.compileShader(rasterizer_shader_dir.str(), "shader.vert");
    shaderHelper.compileShader(rasterizer_shader_dir.str(), "shader.frag");

    File vertexFile(shaderHelper.getShaderSpvDir(rasterizer_shader_dir.str(), "shader.vert"));
    File fragmentFile(shaderHelper.getShaderSpvDir(rasterizer_shader_dir.str(), "shader.frag"));
    std::vector<char> vertex_shader_code = vertexFile.readCharSequence();
    std::vector<char> fragment_shader_code = fragmentFile.readCharSequence();

    // build shader modules to link to graphics pipeline
    VkShaderModule vertex_shader_module = shaderHelper.createShaderModule(device, vertex_shader_code);
    VkShaderModule fragment_shader_module = shaderHelper.createShaderModule(device, fragment_shader_code);

    // shader stage creation information
    // vertex stage creation information
    VkPipelineShaderStageCreateInfo vertex_shader_create_info{};
    vertex_shader_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertex_shader_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_shader_create_info.module = vertex_shader_module;
    vertex_shader_create_info.pName = "main";

    // fragment stage creation information
    VkPipelineShaderStageCreateInfo fragment_shader_create_info{};
    fragment_shader_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragment_shader_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_shader_create_info.module = fragment_shader_module;
    fragment_shader_create_info.pName = "main";

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages = { vertex_shader_create_info,
        fragment_shader_create_info };

    // how the data for a single vertex (including info such as position, color,
    // texture coords, normals, etc) is as a whole
    VkVertexInputBindingDescription binding_description{};
    binding_description.binding = 0;
    binding_description.stride = sizeof(Vertex);
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;// how to move between data after each
                                                                // vertex.

    // how the data for an attribute is defined within a vertex
    std::array<VkVertexInputAttributeDescription, 4> attribute_describtions = vertex::getVertexInputAttributeDesc();

    // CREATE PIPELINE
    // 1.) Vertex input
    VkPipelineVertexInputStateCreateInfo vertex_input_create_info{};
    vertex_input_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_create_info.vertexBindingDescriptionCount = 1;
    vertex_input_create_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_create_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_describtions.size());
    vertex_input_create_info.pVertexAttributeDescriptions = attribute_describtions.data();

    // input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // viewport & scissor
    // create a viewport info struct
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    viewport.width = (float)swap_chain_extent.width;
    viewport.height = (float)swap_chain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // create a scissor info struct
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swap_chain_extent;

    VkPipelineViewportStateCreateInfo viewport_state_create_info{};
    viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports = &viewport;
    viewport_state_create_info.scissorCount = 1;
    viewport_state_create_info.pScissors = &scissor;

    // RASTERIZER
    VkPipelineRasterizationStateCreateInfo rasterizer_create_info{};
    rasterizer_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer_create_info.depthClampEnable = VK_FALSE;
    rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
    rasterizer_create_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer_create_info.lineWidth = 1.0f;
    rasterizer_create_info.cullMode = VK_CULL_MODE_BACK_BIT;//
    // winding to determine which side is front; y-coordinate is inverted in
    // comparison to OpenGL
    rasterizer_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer_create_info.depthBiasClamp = VK_FALSE;

    // -- MULTISAMPLING --
    VkPipelineMultisampleStateCreateInfo multisample_create_info{};
    multisample_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_create_info.sampleShadingEnable = VK_FALSE;
    multisample_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // -- BLENDING --
    // blend attachment state
    VkPipelineColorBlendAttachmentState color_state{};
    color_state.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    color_state.blendEnable = VK_TRUE;
    // blending uses equation: (srcColorBlendFactor * new_color) color_blend_op
    // (dstColorBlendFactor * old_color)
    color_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_state.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending_create_info{};
    color_blending_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending_create_info.logicOpEnable = VK_FALSE;
    color_blending_create_info.attachmentCount = 1;
    color_blending_create_info.pAttachments = &color_state;

    // -- PIPELINE LAYOUT --
    VkPipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    // create pipeline layout
    VkResult result =
      vkCreatePipelineLayout(device->getLogicalDevice(), &pipeline_layout_create_info, nullptr, &pipeline_layout);
    ASSERT_VULKAN(result, "Failed to create pipeline layout!")

    // -- DEPTH STENCIL TESTING --
    VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info{};
    depth_stencil_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_create_info.depthTestEnable = VK_TRUE;
    depth_stencil_create_info.depthWriteEnable = VK_TRUE;
    depth_stencil_create_info.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil_create_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_create_info.stencilTestEnable = VK_FALSE;

    // -- GRAPHICS PIPELINE CREATION --
    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info{};
    graphics_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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

    // pipeline derivatives : can create multiple pipelines that derive from one
    // another for optimization
    graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    graphics_pipeline_create_info.basePipelineIndex = -1;

    // create graphics pipeline
    result = vkCreateGraphicsPipelines(
      device->getLogicalDevice(), VK_NULL_HANDLE, 1, &graphics_pipeline_create_info, nullptr, &graphics_pipeline);
    ASSERT_VULKAN(result, "Failed to create a graphics pipeline!")

    // Destroy shader modules, no longer needed after pipeline created
    vkDestroyShaderModule(device->getLogicalDevice(), vertex_shader_module, nullptr);
    vkDestroyShaderModule(device->getLogicalDevice(), fragment_shader_module, nullptr);
}
