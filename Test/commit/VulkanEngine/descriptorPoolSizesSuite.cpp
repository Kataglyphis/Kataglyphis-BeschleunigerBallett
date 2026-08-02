// Pins deriveDescriptorPoolSizes' contract: one pool-size entry per distinct
// descriptor type, accumulated as descriptorCount * set_count across every
// binding that shares the type. Also pins the regression this extraction was
// about - the shared render descriptor set's storage-buffer slot used to be
// hard-overridden to a byte count (sizeof(ObjectDescription) * MAX_OBJECTS)
// instead of the one descriptor per set it actually needs.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "common/host_device_shared_vars.hpp"

import kataglyphis.vulkan.descriptor_set_group;

namespace {

using Kataglyphis::deriveDescriptorPoolSizes;

vk::DescriptorSetLayoutBinding makeBinding(uint32_t binding, vk::DescriptorType type, uint32_t descriptor_count)
{
    vk::DescriptorSetLayoutBinding layout_binding{};
    layout_binding.binding = binding;
    layout_binding.descriptorType = type;
    layout_binding.descriptorCount = descriptor_count;
    layout_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    return layout_binding;
}

TEST(DescriptorPoolSizesUnit, DerivesCountTimesSetCountPerBinding)
{
    const std::vector<vk::DescriptorSetLayoutBinding> bindings = { makeBinding(0, vk::DescriptorType::eUniformBuffer, 1) };

    const std::vector<vk::DescriptorPoolSize> pool_sizes = deriveDescriptorPoolSizes(bindings, 3);

    ASSERT_EQ(pool_sizes.size(), 1U);
    EXPECT_EQ(pool_sizes[0].type, vk::DescriptorType::eUniformBuffer);
    EXPECT_EQ(pool_sizes[0].descriptorCount, 3U);
}

TEST(DescriptorPoolSizesUnit, AccumulatesBindingsThatShareADescriptorType)
{
    const std::vector<vk::DescriptorSetLayoutBinding> bindings = { makeBinding(0, vk::DescriptorType::eUniformBuffer, 1),
        makeBinding(1, vk::DescriptorType::eUniformBuffer, 2) };

    const std::vector<vk::DescriptorPoolSize> pool_sizes = deriveDescriptorPoolSizes(bindings, 3);

    ASSERT_EQ(pool_sizes.size(), 1U);
    EXPECT_EQ(pool_sizes[0].type, vk::DescriptorType::eUniformBuffer);
    EXPECT_EQ(pool_sizes[0].descriptorCount, 9U);
}

TEST(DescriptorPoolSizesUnit, KeepsOneEntryPerDistinctType)
{
    const std::vector<vk::DescriptorSetLayoutBinding> bindings = { makeBinding(0, vk::DescriptorType::eUniformBuffer, 1),
        makeBinding(1, vk::DescriptorType::eStorageBuffer, 1),
        makeBinding(2, vk::DescriptorType::eCombinedImageSampler, 1) };

    const std::vector<vk::DescriptorPoolSize> pool_sizes = deriveDescriptorPoolSizes(bindings, 2);

    EXPECT_EQ(pool_sizes.size(), 3U);
}

TEST(DescriptorPoolSizesUnit, HandlesAnArrayBinding)
{
    const std::vector<vk::DescriptorSetLayoutBinding> bindings = { makeBinding(
      0, vk::DescriptorType::eSampledImage, static_cast<uint32_t>(MAX_TEXTURE_COUNT)) };

    const std::vector<vk::DescriptorPoolSize> pool_sizes = deriveDescriptorPoolSizes(bindings, 3);

    ASSERT_EQ(pool_sizes.size(), 1U);
    EXPECT_EQ(pool_sizes[0].type, vk::DescriptorType::eSampledImage);
    EXPECT_EQ(pool_sizes[0].descriptorCount, static_cast<uint32_t>(MAX_TEXTURE_COUNT) * 3U);
}

TEST(DescriptorPoolSizesUnit, ReturnsEmptyForNoBindings)
{
    const std::vector<vk::DescriptorSetLayoutBinding> bindings;

    EXPECT_TRUE(deriveDescriptorPoolSizes(bindings, 3).empty());
}

TEST(DescriptorPoolSizesUnit, TheSharedRenderSetNeedsOnlyOneStorageBufferPerSet)
{
    constexpr uint32_t set_count = 3;
    const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        makeBinding(globalUBO_BINDING, vk::DescriptorType::eUniformBuffer, 1),
        makeBinding(sceneUBO_BINDING, vk::DescriptorType::eUniformBuffer, 1),
        makeBinding(OBJECT_DESCRIPTION_BINDING, vk::DescriptorType::eStorageBuffer, 1),
        makeBinding(TEXTURES_BINDING, vk::DescriptorType::eSampledImage, static_cast<uint32_t>(MAX_TEXTURE_COUNT)),
        makeBinding(SAMPLER_BINDING, vk::DescriptorType::eSampler, static_cast<uint32_t>(MAX_TEXTURE_COUNT)),
        makeBinding(SHADOW_MAP_BINDING, vk::DescriptorType::eCombinedImageSampler, 1),
    };

    const std::vector<vk::DescriptorPoolSize> pool_sizes = deriveDescriptorPoolSizes(bindings, set_count);

    const auto storage_buffer_entry =
      std::find_if(pool_sizes.begin(), pool_sizes.end(), [](const vk::DescriptorPoolSize &pool_size) {
          return pool_size.type == vk::DescriptorType::eStorageBuffer;
      });
    ASSERT_NE(storage_buffer_entry, pool_sizes.end());
    EXPECT_EQ(storage_buffer_entry->descriptorCount, set_count);
    EXPECT_NE(storage_buffer_entry->descriptorCount, 1600U);
}

}// namespace
