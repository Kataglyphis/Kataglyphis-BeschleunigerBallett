#include "scene/Mesh.hpp"

#include <cstring>
#include <memory>

#include "common/Utilities.hpp"
#include "vulkan_base/VulkanBuffer.hpp"

Mesh::Mesh() {}

void Mesh::cleanUp()
{
    vertexBuffer.cleanUp();
    indexBuffer.cleanUp();
    objectDescriptionBuffer.cleanUp();
    materialIdsBuffer.cleanUp();
    materialsBuffer.cleanUp();
}

Mesh::Mesh(VulkanDevice *device,
  VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  std::vector<Vertex> &vertices,
  std::vector<uint32_t> &indices,
  std::vector<unsigned int> &materialIndex,
  std::vector<ObjMaterial> &materials)
{
    // glm uses column major matrices so transpose it for Vulkan want row major
    // here
    glm::mat4 transpose_transform = glm::transpose(glm::mat4(1.0f));
    VkTransformMatrixKHR out_matrix;
    std::memcpy(&out_matrix, &transpose_transform, sizeof(VkTransformMatrixKHR));

    index_count = static_cast<uint32_t>(indices.size());
    vertex_count = static_cast<uint32_t>(vertices.size());
    this->device = device;
    object_description = ObjectDescription{};
    createVertexBuffer(transfer_queue, transfer_command_pool, vertices);
    createIndexBuffer(transfer_queue, transfer_command_pool, indices);
    createMaterialIDBuffer(transfer_queue, transfer_command_pool, materialIndex);
    createMaterialBuffer(transfer_queue, transfer_command_pool, materials);

    VkBufferDeviceAddressInfo vertex_info{};
    vertex_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
    vertex_info.buffer = vertexBuffer.getBuffer();

    VkBufferDeviceAddressInfo index_info{};
    index_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
    index_info.buffer = indexBuffer.getBuffer();

    VkBufferDeviceAddressInfo material_index_info{};
    material_index_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
    material_index_info.buffer = materialIdsBuffer.getBuffer();

    VkBufferDeviceAddressInfo material_info{};
    material_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
    material_info.buffer = materialsBuffer.getBuffer();

    object_description.index_address = vkGetBufferDeviceAddress(device->getLogicalDevice(), &index_info);
    object_description.vertex_address = vkGetBufferDeviceAddress(device->getLogicalDevice(), &vertex_info);
    object_description.material_index_address =
      vkGetBufferDeviceAddress(device->getLogicalDevice(), &material_index_info);
    object_description.material_address = vkGetBufferDeviceAddress(device->getLogicalDevice(), &material_info);

    model = glm::mat4(1.0f);
}

void Mesh::setModel(glm::mat4 new_model) { model = new_model; }

Mesh::~Mesh() {}

void Mesh::createVertexBuffer(VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  std::vector<Vertex> &vertices)
{
    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      vertexBuffer,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
      vertices);
}

void Mesh::createIndexBuffer(VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  std::vector<uint32_t> &indices)
{
    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      indexBuffer,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
      indices);
}

void Mesh::createMaterialIDBuffer(VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  std::vector<unsigned int> &materialIndex)
{
    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      materialIdsBuffer,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
      materialIndex);
}

void Mesh::createMaterialBuffer(VkQueue transfer_queue,
  VkCommandPool transfer_command_pool,
  std::vector<ObjMaterial> &materials)
{
    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      materialsBuffer,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
      materials);
}
