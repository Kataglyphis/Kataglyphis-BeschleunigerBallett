#include "PostStage.hpp"

#include <array>
#include <filesystem>
#include <vector>

#include "common/FormatHelper.hpp"
#include "gui/GUI.hpp"
#include "renderer/pushConstants/PushConstantPost.hpp"
#include "scene/Vertex.hpp"
#include "util/File.hpp"
#include "vulkan_base/ShaderHelper.hpp"

#include "renderer/VulkanRendererConfig.hpp"

#include "common/Utilities.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

PostStage::PostStage() {}

void PostStage::init(VulkanDevice *device,
  VulkanSwapChain *vulkanSwapChain,
  const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
{
    this->device = device;
    this->vulkanSwapChain = vulkanSwapChain;

    createOffscreenTextureSampler();

    createPushConstantRange();
    createDepthbufferImage();
    createRenderpass();
    createGraphicsPipeline(descriptorSetLayouts);
    createFramebuffer();
}

void PostStage::shaderHotReload(const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts)
{
    vkDestroyPipeline(device->getLogicalDevice(), graphics_pipeline, nullptr);
    createGraphicsPipeline(descriptor_set_layouts);
}

void PostStage::recordCommands(VkCommandBuffer &commandBuffer,
  uint32_t image_index,
  const std::vector<VkDescriptorSet> &descriptorSets)
{
    // information about how to begin a render pass (only needed for graphical
    // applications)
    VkRenderPassBeginInfo render_pass_begin_info{};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = render_pass;// render pass to begin
    render_pass_begin_info.renderArea.offset = { 0, 0 };// start point of render pass in pixels
    const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    render_pass_begin_info.renderArea.extent = swap_chain_extent;// size of region to run render pass on (starting at
                                                                 // offset)

    // make sure the order you put the values into the array matches with the
    // attchment order you have defined previous
    std::array<VkClearValue, 2> clear_values = {};
    clear_values[0].color = { 0.2f, 0.65f, 0.4f, 1.0f };
    clear_values[1].depthStencil = { 1.0f, 0 };

    render_pass_begin_info.pClearValues = clear_values.data();
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());

    // used framebuffer depends on the swap chain and therefore is changing for
    // each command buffer
    render_pass_begin_info.framebuffer = framebuffers[image_index];

    // begin render pass
    vkCmdBeginRenderPass(commandBuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    auto aspectRatio = static_cast<float>(swap_chain_extent.width) / static_cast<float>(swap_chain_extent.height);
    PushConstantPost pc_post{};
    pc_post.aspect_ratio = aspectRatio;
    vkCmdPushConstants(commandBuffer,
      pipeline_layout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      0,
      sizeof(PushConstantPost),
      &pc_post);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    vkCmdBindDescriptorSets(commandBuffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipeline_layout,
      0,
      static_cast<uint32_t>(descriptorSets.size()),
      descriptorSets.data(),
      0,
      nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    // Rendering gui
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    // end render pass
    vkCmdEndRenderPass(commandBuffer);
}

void PostStage::cleanUp()
{
    depthBufferImage.cleanUp();
    for (auto framebuffer : framebuffers) { vkDestroyFramebuffer(device->getLogicalDevice(), framebuffer, nullptr); }

    vkDestroySampler(device->getLogicalDevice(), offscreenTextureSampler, nullptr);

    vkDestroyRenderPass(device->getLogicalDevice(), render_pass, nullptr);
    vkDestroyPipeline(device->getLogicalDevice(), graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(device->getLogicalDevice(), pipeline_layout, nullptr);
}

PostStage::~PostStage() {}

void PostStage::createDepthbufferImage()
{
    // get supported format for depth buffer
    depth_format = KataglyphisRenderer::choose_supported_format(device->getPhysicalDevice(),
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
    depthBufferImage.createImageView(device, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void PostStage::createOffscreenTextureSampler()
{
    // sampler create info
    VkSamplerCreateInfo sampler_create_info{};
    sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_create_info.magFilter = VK_FILTER_LINEAR;
    sampler_create_info.minFilter = VK_FILTER_LINEAR;
    sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    sampler_create_info.unnormalizedCoordinates = VK_FALSE;
    sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_create_info.mipLodBias = 0.0f;
    sampler_create_info.minLod = 0.0f;
    sampler_create_info.maxLod = 0.0f;
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = 16;// max anisotropy sample level

    VkResult result =
      vkCreateSampler(device->getLogicalDevice(), &sampler_create_info, nullptr, &offscreenTextureSampler);
    ASSERT_VULKAN(result, "Failed to create a texture sampler!")
}

void PostStage::createPushConstantRange()
{
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstantPost);
}

void PostStage::createRenderpass()
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
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;// image data layout before render pass starts
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;// image data layout after render pass
                                                                   // (to change to)

    // depth attachment of render pass
    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = KataglyphisRenderer::choose_supported_format(device->getPhysicalDevice(),
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
    subpass_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;// pipeline stage
    subpass_dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;// stage access mask (memory access)
    subpass_dependencies[0].dstSubpass = 0;
    subpass_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpass_dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    subpass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

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

void PostStage::createGraphicsPipeline(const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
{
    std::stringstream post_shader_dir;
    std::filesystem::path cwd = std::filesystem::current_path();
    post_shader_dir << cwd.string();
    post_shader_dir << RELATIVE_RESOURCE_PATH;
    post_shader_dir << "Shaders/post/";

    std::string post_vert_shader = "post.vert";
    std::string post_frag_shader = "post.frag";

    ShaderHelper shaderHelper;
    File vertexShaderFile(shaderHelper.getShaderSpvDir(post_shader_dir.str(), post_vert_shader));
    std::vector<char> vertex_shader_code = vertexShaderFile.readCharSequence();
    File fragmentShaderFile(shaderHelper.getShaderSpvDir(post_shader_dir.str(), post_frag_shader));
    std::vector<char> fragment_shader_code = fragmentShaderFile.readCharSequence();

    shaderHelper.compileShader(post_shader_dir.str(), post_vert_shader);
    shaderHelper.compileShader(post_shader_dir.str(), post_frag_shader);

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
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 4> attribute_describtions = vertex::getVertexInputAttributeDesc();

    // CREATE PIPELINE
    // 1.) Vertex input
    VkPipelineVertexInputStateCreateInfo vertex_input_create_info{};
    vertex_input_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_create_info.vertexBindingDescriptionCount = 0;
    vertex_input_create_info.pVertexBindingDescriptions = nullptr;
    vertex_input_create_info.vertexAttributeDescriptionCount = 0;
    vertex_input_create_info.pVertexAttributeDescriptions = nullptr;

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
    rasterizer_create_info.cullMode = VK_CULL_MODE_NONE;
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
    color_blending_create_info.logicOpEnable = VK_FALSE;// alternative to calculations is to use logical operations
    color_blending_create_info.logicOp = VK_LOGIC_OP_CLEAR;
    color_blending_create_info.attachmentCount = 1;
    color_blending_create_info.pAttachments = &color_state;
    for (int i = 0; i < 4; i++) { color_blending_create_info.blendConstants[0] = 0.f; }
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
    depth_stencil_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
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

void PostStage::createFramebuffer()
{
    // resize framebuffer size to equal swap chain image count
    framebuffers.resize(vulkanSwapChain->getNumberSwapChainImages());

    for (size_t i = 0; i < vulkanSwapChain->getNumberSwapChainImages(); i++) {
        Texture &swap_chain_image = vulkanSwapChain->getSwapChainImage(i);

        std::array<VkImageView, 2> attachments = { swap_chain_image.getImageView(), depthBufferImage.getImageView() };

        VkFramebufferCreateInfo frame_buffer_create_info{};
        frame_buffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frame_buffer_create_info.renderPass = render_pass;// render pass layout the framebuffer will be used with
        frame_buffer_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        frame_buffer_create_info.pAttachments = attachments.data();// list of attachments (1:1 with render pass)
        const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
        frame_buffer_create_info.width = swap_chain_extent.width;// framebuffer width
        frame_buffer_create_info.height = swap_chain_extent.height;// framebuffer height
        frame_buffer_create_info.layers = 1;// framebuffer layer

        VkResult result =
          vkCreateFramebuffer(device->getLogicalDevice(), &frame_buffer_create_info, nullptr, &framebuffers[i]);
        ASSERT_VULKAN(result, "Failed to create framebuffer!")
    }
}
