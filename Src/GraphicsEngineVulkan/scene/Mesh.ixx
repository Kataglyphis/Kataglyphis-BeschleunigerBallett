module;
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.mesh;

import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.object_description;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.buffer_manager;

export namespace Kataglyphis {
// this a simple Mesh without mesh generation
class Mesh
{
  public:
    Mesh(VulkanDevice *device,
      vk::Queue transfer_queue,
      vk::CommandPool transfer_command_pool,
      std::vector<Vertex> &vertices,
      std::vector<uint32_t> &indices,
      std::vector<unsigned int> &materialIndex,
      std::vector<ObjMaterial> &materials);

    Mesh();
    Mesh(const Mesh &) = delete;
    auto operator=(const Mesh &) -> Mesh & = delete;
    Mesh(Mesh &&other) noexcept = default;
    auto operator=(Mesh &&other) noexcept -> Mesh & = default;

    void cleanUp();

    ObjectDescription &getObjectDescription() { return object_description; };
    glm::mat4 getModel() { return model; };
    uint32_t getVertexCount() { return vertex_count; };
    uint32_t getIndexCount() { return index_count; };
    vk::Buffer &getVertexBuffer() { return vertexBuffer.getBuffer(); };
    vk::Buffer &getMaterialIDBuffer() { return materialIdsBuffer.getBuffer(); };
    vk::Buffer &getIndexBuffer() { return indexBuffer.getBuffer(); };

    void setModel(glm::mat4 new_model);

    ~Mesh();

  private:
    VulkanBufferManager vulkanBufferManager;

    static constexpr uint64_t INVALID_ADDR = ~uint64_t(0);
    ObjectDescription object_description{ INVALID_ADDR,
        INVALID_ADDR,
        INVALID_ADDR,
        INVALID_ADDR };

    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;
    VulkanBuffer objectDescriptionBuffer;
    VulkanBuffer materialIdsBuffer;
    VulkanBuffer materialsBuffer;

    glm::mat4 model{};

    static constexpr uint32_t INVALID_COUNT = ~uint32_t(0);
    uint32_t vertex_count{ INVALID_COUNT };
    uint32_t index_count{ INVALID_COUNT };

    VulkanDevice *device{ nullptr };

    void createVertexBuffer(vk::Queue transfer_queue, vk::CommandPool transfer_command_pool, std::vector<Vertex> &vertices);

    void createIndexBuffer(vk::Queue transfer_queue, vk::CommandPool transfer_command_pool, std::vector<uint32_t> &indices);

    void createMaterialIDBuffer(vk::Queue transfer_queue,
      vk::CommandPool transfer_command_pool,
      std::vector<unsigned int> &materialIndex);

    void createMaterialBuffer(vk::Queue transfer_queue,
      vk::CommandPool transfer_command_pool,
      std::vector<ObjMaterial> &materials);
};
}// namespace Kataglyphis
