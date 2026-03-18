module;

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

Model::Model(VulkanDevice *device) : device(device) {}

void Model::cleanUp()
{
    for (Texture &texture : modelTextures) { texture.cleanUp(); }

    for (vk::Sampler texture_sampler : modelTextureSamplers) {
        device->getLogicalDevice().destroySampler(texture_sampler);
    }

    mesh.cleanUp();
}

void Model::add_new_mesh(VulkanDevice *vulkan_device,
  vk::Queue transfer_queue,
  vk::CommandPool command_pool,
  std::vector<Vertex> &vertices,
  std::vector<unsigned int> &indices,
  std::vector<unsigned int> &materialIndex,
  std::vector<ObjMaterial> &materials)
{
    this->mesh = Mesh(vulkan_device, transfer_queue, command_pool, vertices, indices, materialIndex, materials);
}

void Model::set_model(glm::mat4 new_model) { this->model = new_model; }

void Model::addTexture(Texture &&newTexture)
{
    modelTextures.emplace_back(std::move(newTexture));
    addSampler(modelTextures.back());
}

auto Model::getPrimitiveCount() -> uint32_t
{
    /*uint32_t number_of_indices = 0;

      for (Mesh mesh : meshes) {

          number_of_indices += mesh.get_index_count();

      }

      return number_of_indices / 3;*/
    return mesh.getIndexCount() / 3;
}

Model::~Model() = default;

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

    vk::Sampler newSampler = device->getLogicalDevice().createSampler(sampler_create_info);

    modelTextureSamplers.push_back(newSampler);
}