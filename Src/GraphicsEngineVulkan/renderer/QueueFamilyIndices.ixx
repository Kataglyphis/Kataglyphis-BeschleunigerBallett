module;

export module kataglyphis.vulkan.queue_family_indices;

export namespace Kataglyphis::VulkanRendererInternals {
struct QueueFamilyIndices
{
    int graphics_family = -1;
    int presentation_family = -1;
    int compute_family = -1;

    bool is_valid() const { return graphics_family >= 0 && presentation_family >= 0 && compute_family >= 0; }
};
}// namespace Kataglyphis::VulkanRendererInternals
