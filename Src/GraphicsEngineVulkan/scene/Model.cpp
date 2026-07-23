module;
#include <memory>

#include "common/Utilities.hpp"
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.model;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.vertex;

using namespace Kataglyphis;

Model::Model() = default;

Model::Model(std::shared_ptr<VulkanDevice>device) : device(device) {}

void Model::cleanUp()
{
    if (device == nullptr) return;
    for (Texture &texture : modelTextures) { texture.cleanUp(); }
    modelTextures.clear();

    for (vk::Sampler texture_sampler : modelTextureSamplers) {
        device->getLogicalDevice().destroySampler(texture_sampler);
    }
    modelTextureSamplers.clear();

    for (Mesh &m : meshes) { m.cleanUp(); }
}

void Model::add_new_mesh(std::shared_ptr<VulkanDevice>vulkan_device,
  vk::Queue transfer_queue,
  vk::CommandPool command_pool,
  std::vector<Vertex> &vertices,
  std::vector<unsigned int> &indices,
  std::vector<unsigned int> &materialIndex,
  std::vector<ObjMaterial> &materials)
{
    // Append, not overwrite: the loaders call this once per Model today, so this
    // is behaviour-identical, but it is what lets a Model hold several meshes.
    meshes.emplace_back(vulkan_device, transfer_queue, command_pool, vertices, indices, materialIndex, materials);
}

void Model::set_model(glm::mat4 new_model) { this->model = new_model; }

void Model::addTexture(Texture &&newTexture)
{
    modelTextures.emplace_back(std::move(newTexture));
    addSampler(modelTextures.back());
}

auto Model::getPrimitiveCount() -> uint32_t
{
    uint32_t number_of_indices = 0;
    for (Mesh &m : meshes) { number_of_indices += m.getIndexCount(); }
    return number_of_indices / 3;
}

Model::~Model() { cleanUp(); }

void Model::addSampler(const Texture &newTexture)
{
    vk::PhysicalDeviceFeatures physical_device_features = device->getPhysicalDevice().getFeatures();

    vk::SamplerCreateInfo sampler_create_info{};
    sampler_create_info.magFilter = vk::Filter::eLinear;
    sampler_create_info.minFilter = vk::Filter::eLinear;
    sampler_create_info.addressModeU = vk::SamplerAddressMode::eRepeat;
    sampler_create_info.addressModeV = vk::SamplerAddressMode::eRepeat;
    sampler_create_info.addressModeW = vk::SamplerAddressMode::eRepeat;
    sampler_create_info.borderColor = vk::BorderColor::eFloatOpaqueBlack;
    sampler_create_info.unnormalizedCoordinates = VK_FALSE;
    sampler_create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    sampler_create_info.mipLodBias = 0.0F;
    sampler_create_info.minLod = 0.0F;
    sampler_create_info.maxLod = static_cast<float>(newTexture.getMipLevel());
    sampler_create_info.anisotropyEnable = physical_device_features.samplerAnisotropy;
    sampler_create_info.maxAnisotropy = (physical_device_features.samplerAnisotropy != 0u) ? 16.0F : 1.0F;

    vk::ResultValue<vk::Sampler> sampler_result = device->getLogicalDevice().createSampler(sampler_create_info);
    vk::Sampler newSampler = sampler_result.value;

    modelTextureSamplers.push_back(newSampler);
}