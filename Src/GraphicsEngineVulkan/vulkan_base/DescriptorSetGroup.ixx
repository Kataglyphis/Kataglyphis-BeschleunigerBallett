module;
#include <cstdint>

#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.descriptor_set_group;

import kataglyphis.vulkan.device;

export namespace Kataglyphis {
// Owns ONE descriptor set layout + descriptor pool + N descriptor sets
// (typically one per swapchain image). Replaces the four structurally
// identical layout/pool/set triads that used to live in VulkanRenderer.
//
// Usage: declare the bindings with addBinding(), then create(device, N).
// Pool sizes are derived from the bindings (count * set_count per type);
// setPoolSize() overrides the derived size for a single descriptor type.
// The write helpers look up descriptor type and array count from the
// declared binding, so call sites stay declarative.
//
// cleanUp() is idempotent and also clears the recorded bindings, so a
// swapchain recreation can rebuild the group from a fresh declarative
// binding list. Move-only with a destructor safety net, matching the
// established leaf-RAII pattern (VulkanBuffer et al.).
class DescriptorSetGroup
{
  public:
    DescriptorSetGroup();
    DescriptorSetGroup(const DescriptorSetGroup &) = delete;
    DescriptorSetGroup &operator=(const DescriptorSetGroup &) = delete;
    DescriptorSetGroup(DescriptorSetGroup &&other) noexcept;
    DescriptorSetGroup &operator=(DescriptorSetGroup &&other) noexcept;

    DescriptorSetGroup &addBinding(uint32_t binding,
      vk::DescriptorType type,
      uint32_t descriptor_count,
      vk::ShaderStageFlags stages);
    // Overrides the pool size derived from the bindings for one descriptor
    // type (e.g. the historical oversized storage-buffer pool slot).
    DescriptorSetGroup &setPoolSize(vk::DescriptorType type, uint32_t descriptor_count);

    // Creates layout + pool and allocates set_count sets. Logs and returns
    // false on failure (partially created objects are cleaned up).
    [[nodiscard]] bool create(std::shared_ptr<VulkanDevice> vulkan_device, uint32_t set_count);

    // -- write helpers (thin wrappers around vkUpdateDescriptorSets; the
    // descriptor type / array count come from the declared binding)
    void writeBuffer(uint32_t set_index,
      uint32_t binding,
      vk::Buffer buffer,
      vk::DeviceSize range,
      vk::DeviceSize offset = 0);
    // sampler may be null (sampled images, storage images, input attachments)
    void writeImage(uint32_t set_index,
      uint32_t binding,
      vk::ImageView image_view,
      vk::ImageLayout image_layout,
      vk::Sampler sampler = nullptr);
    // Writes the binding's whole descriptor array; infos.size() must match
    // the declared descriptor count.
    void writeImageArray(uint32_t set_index, uint32_t binding, const std::vector<vk::DescriptorImageInfo> &infos);
    void writeAccelerationStructure(uint32_t set_index, uint32_t binding, const vk::AccelerationStructureKHR &tlas);

    [[nodiscard]] vk::DescriptorSetLayout getLayout() const { return layout; }
    [[nodiscard]] const std::vector<vk::DescriptorSet> &sets() const { return descriptor_sets; }

    // Destroys pool + layout, frees the sets and forgets the declared
    // bindings/pool-size overrides. Safe to call multiple times.
    void cleanUp();

    ~DescriptorSetGroup();

  private:
    // Returns nullptr (and logs) when the binding was never declared.
    const vk::DescriptorSetLayoutBinding *findBinding(uint32_t binding) const;
    bool checkWritePreconditions(uint32_t set_index, uint32_t binding) const;

    std::shared_ptr<VulkanDevice> device{ nullptr };

    // Value-initialize ({}) so the vectors are empty regardless of which
    // translation unit constructs this class. Without the braces, a C++23
    // module ABI mismatch can leave the vectors with garbage internal state
    // (the module interface unit's default init and the implementation
    // unit's constructor definition disagree on whether to zero-init).
    std::vector<vk::DescriptorSetLayoutBinding> bindings{};
    std::vector<vk::DescriptorPoolSize> pool_size_overrides{};

    vk::DescriptorSetLayout layout{};
    vk::DescriptorPool pool{};
    std::vector<vk::DescriptorSet> descriptor_sets{};
};
}// namespace Kataglyphis
