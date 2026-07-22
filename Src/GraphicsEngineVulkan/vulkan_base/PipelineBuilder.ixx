module;
#include <cstdint>

#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.pipeline_builder;

export namespace Kataglyphis {
// Fluent builder for the graphics pipeline construction that was previously
// copy-pasted across the render stages. Defaults match the most common
// variant: triangle list, dynamic viewport/scissor, fill mode, back-face
// culling, counter-clockwise front face, no blending on a single color
// attachment, depth test + write with CompareOp::eLess, single sample.
// Pipeline LAYOUT creation stays in the stages; this builder only builds
// the vk::Pipeline.
class PipelineBuilder
{
  public:
    PipelineBuilder();

    PipelineBuilder &setShaderStages(std::vector<vk::PipelineShaderStageCreateInfo> shader_stages);
    PipelineBuilder &setVertexInput(std::vector<vk::VertexInputBindingDescription> bindings,
      std::vector<vk::VertexInputAttributeDescription> attributes);
    PipelineBuilder &setCullMode(vk::CullModeFlags cull_mode);
    PipelineBuilder &setFrontFace(vk::FrontFace front_face);
    // When enabled uses the standard alpha blending factors
    // (srcAlpha/oneMinusSrcAlpha for color, one/zero for alpha, both eAdd).
    PipelineBuilder &setAlphaBlending(bool enable);
    PipelineBuilder &setColorAttachmentCount(uint32_t count);
    // Pass false for depth-only pipelines (no pColorBlendState at all).
    PipelineBuilder &setUseColorBlendState(bool use_color_blend_state);
    PipelineBuilder &setDepthTest(bool enable);
    PipelineBuilder &setDepthWrite(bool enable);
    PipelineBuilder &setDepthCompareOp(vk::CompareOp compare_op);
    // Depth clamp ("shadow pancaking"): fragments nearer than the near plane
    // clamp to depth 0 instead of being CLIPPED. Only legal when the device
    // feature is enabled - pass VulkanDevice::supportsDepthClamp().
    PipelineBuilder &setDepthClamp(bool enable);
    PipelineBuilder &setBasePipelineIndex(int32_t base_pipeline_index);

    // Creates the pipeline via createGraphicsPipelines(pipeline_cache, ...);
    // aborts through ASSERT_VULKAN with error_message on failure.
    // pipeline_cache may be null (no caching).
    [[nodiscard]] vk::Pipeline build(vk::Device device,
      vk::PipelineLayout pipeline_layout,
      vk::RenderPass render_pass,
      vk::PipelineCache pipeline_cache = nullptr,
      uint32_t subpass = 0,
      const char *error_message = "Failed to create a graphics pipeline!") const;

  private:
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
    std::vector<vk::VertexInputBindingDescription> vertex_bindings;
    std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
    vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;
    vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;
    bool alpha_blending = false;
    uint32_t color_attachment_count = 1;
    bool use_color_blend_state = true;
    bool depth_test = true;
    bool depth_write = true;
    vk::CompareOp depth_compare_op = vk::CompareOp::eLess;
    bool depth_clamp = false;
    int32_t base_pipeline_index = 0;
};
}// namespace Kataglyphis
