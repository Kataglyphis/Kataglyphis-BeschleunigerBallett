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
import kataglyphis.vulkan.sampler_builder;

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
  vk::CommandPool command_pool,
  std::vector<Vertex> &vertices,
  std::vector<unsigned int> &indices,
  std::vector<unsigned int> &materialIndex,
  std::vector<ObjMaterial> &materials,
  bool double_sided)
{
    // Append, not overwrite: the loaders call this once per Model today, so this
    // is behaviour-identical, but it is what lets a Model hold several meshes.
    meshes.emplace_back(vulkan_device, command_pool, vertices, indices, materialIndex, materials);
    // The Mesh constructor is deliberately unchanged; the per-material doubleSided
    // flag rides in separately (default false = back-face culled).
    meshes.back().setDoubleSided(double_sided);
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
    const bool aniso = device->supportsSamplerAnisotropy();

    vk::SamplerCreateInfo sampler_create_info = buildSamplerCreateInfo(vk::Filter::eLinear,
      vk::SamplerAddressMode::eRepeat,
      static_cast<float>(newTexture.getMipLevel()),
      aniso,
      aniso ? 16.0F : 1.0F,
      vk::BorderColor::eFloatOpaqueBlack);

    vk::ResultValue<vk::Sampler> sampler_result = device->getLogicalDevice().createSampler(sampler_create_info);
    ASSERT_VULKAN(sampler_result.result, "Failed to create texture sampler!")
    vk::Sampler newSampler = sampler_result.value;

    modelTextureSamplers.push_back(newSampler);
}