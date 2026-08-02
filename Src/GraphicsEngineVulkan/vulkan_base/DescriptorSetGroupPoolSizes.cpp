module;
#include <cstdint>

#include <algorithm>
#include <span>
#include <vector>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.descriptor_set_group;

// Split into its own implementation unit (of the SAME module as
// DescriptorSetGroup.cpp) deliberately: deriveDescriptorPoolSizes() touches
// no VulkanDevice, so keeping it out of the TU that does lets it be called
// - and tested - without pulling device machinery in, matching the same
// reasoning in CascadedShadowMapMath.cpp.

std::vector<vk::DescriptorPoolSize> Kataglyphis::deriveDescriptorPoolSizes(
  std::span<const vk::DescriptorSetLayoutBinding> bindings, uint32_t set_count)
{
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    for (const vk::DescriptorSetLayoutBinding &binding : bindings) {
        const auto existing = std::find_if(pool_sizes.begin(), pool_sizes.end(), [&](const vk::DescriptorPoolSize &pool_size) {
            return pool_size.type == binding.descriptorType;
        });
        const uint32_t descriptor_count = binding.descriptorCount * set_count;
        if (existing != pool_sizes.end()) {
            existing->descriptorCount += descriptor_count;
        } else {
            pool_sizes.push_back(vk::DescriptorPoolSize{ binding.descriptorType, descriptor_count });
        }
    }
    return pool_sizes;
}
