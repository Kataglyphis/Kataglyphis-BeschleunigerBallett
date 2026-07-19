module;
#include <cstdint>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "spdlog/spdlog.h"

module kataglyphis.vulkan.descriptor_set_group;

import kataglyphis.vulkan.device;

Kataglyphis::DescriptorSetGroup::DescriptorSetGroup() = default;

Kataglyphis::DescriptorSetGroup::DescriptorSetGroup(DescriptorSetGroup &&other) noexcept
  : device(std::move(other.device)), bindings(std::move(other.bindings)),
    pool_size_overrides(std::move(other.pool_size_overrides)), layout(other.layout), pool(other.pool),
    descriptor_sets(std::move(other.descriptor_sets))
{
    other.layout = nullptr;
    other.pool = nullptr;
    other.bindings.clear();
    other.pool_size_overrides.clear();
    other.descriptor_sets.clear();
}

auto Kataglyphis::DescriptorSetGroup::operator=(DescriptorSetGroup &&other) noexcept -> DescriptorSetGroup &
{
    if (this != &other) {
        cleanUp();

        device = std::move(other.device);
        bindings = std::move(other.bindings);
        pool_size_overrides = std::move(other.pool_size_overrides);
        layout = other.layout;
        pool = other.pool;
        descriptor_sets = std::move(other.descriptor_sets);

        other.layout = nullptr;
        other.pool = nullptr;
        other.bindings.clear();
        other.pool_size_overrides.clear();
        other.descriptor_sets.clear();
    }

    return *this;
}

auto Kataglyphis::DescriptorSetGroup::addBinding(uint32_t binding,
  vk::DescriptorType type,
  uint32_t descriptor_count,
  vk::ShaderStageFlags stages) -> DescriptorSetGroup &
{
    vk::DescriptorSetLayoutBinding layout_binding{};
    layout_binding.binding = binding;
    layout_binding.descriptorType = type;
    layout_binding.descriptorCount = descriptor_count;
    layout_binding.stageFlags = stages;
    layout_binding.pImmutableSamplers = nullptr;
    bindings.push_back(layout_binding);
    return *this;
}

auto Kataglyphis::DescriptorSetGroup::setPoolSize(vk::DescriptorType type, uint32_t descriptor_count)
  -> DescriptorSetGroup &
{
    for (vk::DescriptorPoolSize &pool_size : pool_size_overrides) {
        if (pool_size.type == type) {
            pool_size.descriptorCount = descriptor_count;
            return *this;
        }
    }
    pool_size_overrides.push_back(vk::DescriptorPoolSize{ type, descriptor_count });
    return *this;
}

bool Kataglyphis::DescriptorSetGroup::create(std::shared_ptr<VulkanDevice> vulkan_device, uint32_t set_count)
{
    device = std::move(vulkan_device);

    if (bindings.empty() || set_count == 0) {
        spdlog::error("DescriptorSetGroup::create called without bindings or with zero sets.");
        return false;
    }

    // -- layout
    vk::DescriptorSetLayoutCreateInfo layout_create_info{};
    layout_create_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_create_info.pBindings = bindings.data();

    auto layout_result = device->getLogicalDevice().createDescriptorSetLayout(layout_create_info);
    if (layout_result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create a descriptor set layout!");
        return false;
    }
    layout = layout_result.value;

    // -- pool (sizes derived per descriptor type: binding count * set count,
    // unless overridden via setPoolSize)
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    for (const vk::DescriptorSetLayoutBinding &binding : bindings) {
        const bool overridden = std::any_of(pool_size_overrides.begin(),
          pool_size_overrides.end(),
          [&](const vk::DescriptorPoolSize &pool_size) { return pool_size.type == binding.descriptorType; });
        if (overridden) { continue; }

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
    pool_sizes.insert(pool_sizes.end(), pool_size_overrides.begin(), pool_size_overrides.end());

    vk::DescriptorPoolCreateInfo pool_create_info{};
    pool_create_info.maxSets = set_count;
    pool_create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_create_info.pPoolSizes = pool_sizes.data();

    auto pool_result = device->getLogicalDevice().createDescriptorPool(pool_create_info);
    if (pool_result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create a descriptor pool!");
        device->getLogicalDevice().destroyDescriptorSetLayout(layout);
        layout = nullptr;
        return false;
    }
    pool = pool_result.value;

    // -- sets
    std::vector<vk::DescriptorSetLayout> set_layouts(set_count, layout);

    vk::DescriptorSetAllocateInfo set_alloc_info{};
    set_alloc_info.descriptorPool = pool;
    set_alloc_info.descriptorSetCount = set_count;
    set_alloc_info.pSetLayouts = set_layouts.data();

    auto alloc_result = device->getLogicalDevice().allocateDescriptorSets(set_alloc_info);
    if (alloc_result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to allocate descriptor sets!");
        device->getLogicalDevice().destroyDescriptorPool(pool);
        pool = nullptr;
        device->getLogicalDevice().destroyDescriptorSetLayout(layout);
        layout = nullptr;
        return false;
    }
    descriptor_sets = alloc_result.value;

    return true;
}

auto Kataglyphis::DescriptorSetGroup::findBinding(uint32_t binding) const -> const vk::DescriptorSetLayoutBinding *
{
    for (const vk::DescriptorSetLayoutBinding &layout_binding : bindings) {
        if (layout_binding.binding == binding) { return &layout_binding; }
    }
    spdlog::error("DescriptorSetGroup: binding {} was never declared.", binding);
    return nullptr;
}

bool Kataglyphis::DescriptorSetGroup::checkWritePreconditions(uint32_t set_index, uint32_t binding) const
{
    if (!device || set_index >= descriptor_sets.size()) {
        spdlog::error("DescriptorSetGroup: write to binding {} with invalid set index {}.", binding, set_index);
        return false;
    }
    return true;
}

void Kataglyphis::DescriptorSetGroup::writeBuffer(uint32_t set_index,
  uint32_t binding,
  vk::Buffer buffer,
  vk::DeviceSize range,
  vk::DeviceSize offset)
{
    if (!checkWritePreconditions(set_index, binding)) { return; }
    const vk::DescriptorSetLayoutBinding *layout_binding = findBinding(binding);
    if (layout_binding == nullptr) { return; }

    vk::DescriptorBufferInfo buffer_info{};
    buffer_info.buffer = buffer;
    buffer_info.offset = offset;
    buffer_info.range = range;

    vk::WriteDescriptorSet descriptor_write{};
    descriptor_write.dstSet = descriptor_sets[set_index];
    descriptor_write.dstBinding = binding;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = layout_binding->descriptorType;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pBufferInfo = &buffer_info;

    device->getLogicalDevice().updateDescriptorSets(1, &descriptor_write, 0, nullptr);
}

void Kataglyphis::DescriptorSetGroup::writeImage(uint32_t set_index,
  uint32_t binding,
  vk::ImageView image_view,
  vk::ImageLayout image_layout,
  vk::Sampler sampler)
{
    if (!checkWritePreconditions(set_index, binding)) { return; }
    const vk::DescriptorSetLayoutBinding *layout_binding = findBinding(binding);
    if (layout_binding == nullptr) { return; }

    vk::DescriptorImageInfo image_info{};
    image_info.imageLayout = image_layout;
    image_info.imageView = image_view;
    image_info.sampler = sampler;

    vk::WriteDescriptorSet descriptor_write{};
    descriptor_write.dstSet = descriptor_sets[set_index];
    descriptor_write.dstBinding = binding;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = layout_binding->descriptorType;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pImageInfo = &image_info;

    device->getLogicalDevice().updateDescriptorSets(1, &descriptor_write, 0, nullptr);
}

void Kataglyphis::DescriptorSetGroup::writeImageArray(uint32_t set_index,
  uint32_t binding,
  const std::vector<vk::DescriptorImageInfo> &infos)
{
    if (!checkWritePreconditions(set_index, binding)) { return; }
    const vk::DescriptorSetLayoutBinding *layout_binding = findBinding(binding);
    if (layout_binding == nullptr) { return; }
    if (infos.size() != layout_binding->descriptorCount) {
        spdlog::error("DescriptorSetGroup: image array write to binding {} with {} infos (declared {}).",
          binding,
          infos.size(),
          layout_binding->descriptorCount);
        return;
    }

    vk::WriteDescriptorSet descriptor_write{};
    descriptor_write.dstSet = descriptor_sets[set_index];
    descriptor_write.dstBinding = binding;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = layout_binding->descriptorType;
    descriptor_write.descriptorCount = layout_binding->descriptorCount;
    descriptor_write.pImageInfo = infos.data();

    device->getLogicalDevice().updateDescriptorSets(1, &descriptor_write, 0, nullptr);
}

void Kataglyphis::DescriptorSetGroup::writeAccelerationStructure(uint32_t set_index,
  uint32_t binding,
  const vk::AccelerationStructureKHR &tlas)
{
    if (!checkWritePreconditions(set_index, binding)) { return; }
    const vk::DescriptorSetLayoutBinding *layout_binding = findBinding(binding);
    if (layout_binding == nullptr) { return; }

    vk::WriteDescriptorSetAccelerationStructureKHR acceleration_structure_info{};
    acceleration_structure_info.accelerationStructureCount = 1;
    acceleration_structure_info.pAccelerationStructures = &tlas;

    vk::WriteDescriptorSet descriptor_write{};
    descriptor_write.pNext = &acceleration_structure_info;
    descriptor_write.dstSet = descriptor_sets[set_index];
    descriptor_write.dstBinding = binding;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = layout_binding->descriptorType;
    descriptor_write.descriptorCount = 1;

    device->getLogicalDevice().updateDescriptorSets(1, &descriptor_write, 0, nullptr);
}

void Kataglyphis::DescriptorSetGroup::cleanUp()
{
    if (device) {
        if (pool) { device->getLogicalDevice().destroyDescriptorPool(pool); }
        if (layout) { device->getLogicalDevice().destroyDescriptorSetLayout(layout); }
    }

    pool = nullptr;
    layout = nullptr;
    descriptor_sets.clear();
    bindings.clear();
    pool_size_overrides.clear();
    device = nullptr;
}

Kataglyphis::DescriptorSetGroup::~DescriptorSetGroup() { cleanUp(); }
