#include "Model.hpp"

#include "common/Utilities.hpp"
#include <iostream>
#include <unordered_map>

Model::Model() {}

Model::Model(VulkanDevice *device) { this->device = device; }

void Model::cleanUp()
{
    for (Texture texture : modelTextures) { texture.cleanUp(); }

    for (VkSampler texture_sampler : modelTextureSamplers) {
        vkDestroySampler(device->getLogicalDevice(), texture_sampler, nullptr);
    }

    mesh.cleanUp();
}

void Model::add_new_mesh(VulkanDevice *device,
  VkQueue transfer_queue,
  VkCommandPool command_pool,
  std::vector<Vertex> &vertices,
  std::vector<unsigned int> &indices,
  std::vector<unsigned int> &materialIndex,
  std::vector<ObjMaterial> &materials)
{
    this->mesh = Mesh(device, transfer_queue, command_pool, vertices, indices, materialIndex, materials);
}

void Model::set_model(glm::mat4 model) { this->model = model; }

void Model::addTexture(Texture newTexture)
{
    modelTextures.push_back(newTexture);
    addSampler(newTexture);
}

uint32_t Model::getPrimitiveCount()
{
    /*uint32_t number_of_indices = 0;

      for (Mesh mesh : meshes) {

          number_of_indices += mesh.get_index_count();

      }

      return number_of_indices / 3;*/
    return mesh.getIndexCount() / 3;
}

Model::~Model() {}

void Model::addSampler(Texture newTexture)
{
    VkSampler newSampler;
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
    sampler_create_info.mipLodBias = 0.0f;
    sampler_create_info.minLod = 0.0f;
    sampler_create_info.maxLod = newTexture.getMipLevel();
    sampler_create_info.anisotropyEnable = VK_TRUE;
    sampler_create_info.maxAnisotropy = 16;// max anisotropy sample level

    VkResult result = vkCreateSampler(device->getLogicalDevice(), &sampler_create_info, nullptr, &newSampler);
    ASSERT_VULKAN(result, "Failed to create a texture sampler!")

    modelTextureSamplers.push_back(newSampler);
}
