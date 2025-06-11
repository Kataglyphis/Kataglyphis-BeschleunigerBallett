#pragma once
// Indices (locations) of Queue families (if they exist at all)
namespace Kataglyphis::VulkanRendererInternals {
struct QueueFamilyIndices
{
    int graphics_family = -1;// location of graphics family
    int presentation_family = -1;// location of presentation queue family
    int compute_family = -1;// location of compute queue family

    // check if queue families are valid
    bool is_valid() { return graphics_family >= 0 && presentation_family >= 0 && compute_family >= 0; }
};
}// namespace Kataglyphis::VulkanRendererInternals