#pragma once
#include <glm/glm.hpp>
#include <vector>

#include "ObjectDescription.hpp"
#include "scene/ObjMaterial.hpp"
#include "scene/Vertex.hpp"
#include "vulkan_base/VulkanBufferManager.hpp"

namespace Kataglyphis {
// this a simple Mesh without mesh generation
class Mesh
{
  public:
    Mesh(VulkanDevice *device,
      VkQueue transfer_queue,
      VkCommandPool transfer_command_pool,
      std::vector<Vertex> &vertices,
      std::vector<uint32_t> &indices,
      std::vector<unsigned int> &materialIndex,
      std::vector<ObjMaterial> &materials);

    Mesh();

    void cleanUp();

    ObjectDescription &getObjectDescription() { return object_description; };
    glm::mat4 getModel() { return model; };
    uint32_t getVertexCount() { return vertex_count; };
    uint32_t getIndexCount() { return index_count; };
    VkBuffer &getVertexBuffer() { return vertexBuffer.getBuffer(); };
    VkBuffer &getMaterialIDBuffer() { return materialIdsBuffer.getBuffer(); };
    VkBuffer &getIndexBuffer() { return indexBuffer.getBuffer(); };

    void setModel(glm::mat4 new_model);

    ~Mesh();

  private:
    VulkanBufferManager vulkanBufferManager;

    ObjectDescription object_description{ static_cast<uint64_t>(-1),
        static_cast<uint64_t>(-1),
        static_cast<uint64_t>(-1),
        static_cast<uint64_t>(-1) };

    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;
    VulkanBuffer objectDescriptionBuffer;
    VulkanBuffer materialIdsBuffer;
    VulkanBuffer materialsBuffer;

    glm::mat4 model;

    uint32_t vertex_count{ static_cast<uint32_t>(-1) };
    uint32_t index_count{ static_cast<uint32_t>(-1) };

    VulkanDevice *device{ VK_NULL_HANDLE };

    void createVertexBuffer(VkQueue transfer_queue, VkCommandPool transfer_command_pool, std::vector<Vertex> &vertices);

    void createIndexBuffer(VkQueue transfer_queue, VkCommandPool transfer_command_pool, std::vector<uint32_t> &indices);

    void createMaterialIDBuffer(VkQueue transfer_queue,
      VkCommandPool transfer_command_pool,
      std::vector<unsigned int> &materialIndex);

    void createMaterialBuffer(VkQueue transfer_queue,
      VkCommandPool transfer_command_pool,
      std::vector<ObjMaterial> &materials);
};
}// namespace Kataglyphis