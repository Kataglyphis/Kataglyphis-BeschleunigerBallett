module;

#include "common/Utilities.hpp"
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

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

    for (VkSampler texture_sampler : modelTextureSamplers) {
        vkDestroySampler(device->getLogicalDevice(), texture_sampler, nullptr);
    }

    mesh.cleanUp();
}

void Model::add_new_mesh(VulkanDevice *vulkan_device,
  VkQueue transfer_queue,
  VkCommandPool command_pool,
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
    VkSampler newSampler = nullptr;
    VkPhysicalDeviceFeatures physical_device_features{};
    vkGetPhysicalDeviceFeatures(device->getPhysicalDevice(), &physical_device_features);

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
    sampler_create_info.mipLodBias = 0.0F;
    sampler_create_info.minLod = 0.0F;
    sampler_create_info.maxLod = static_cast<float>(newTexture.getMipLevel());
    sampler_create_info.anisotropyEnable = physical_device_features.samplerAnisotropy;
    sampler_create_info.maxAnisotropy = (physical_device_features.samplerAnisotropy != 0u) ? 16.0F : 1.0F;

    VkResult const result = vkCreateSampler(device->getLogicalDevice(), &sampler_create_info, nullptr, &newSampler);
    ASSERT_VULKAN(result, "Failed to create a texture sampler!")

    modelTextureSamplers.push_back(newSampler);
}
