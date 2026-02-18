#include "scene/Mesh.hpp"

#include <cstdint>
#include <cstring>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/matrix.hpp>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "scene/ObjMaterial.hpp"
#include "scene/Vertex.hpp"
#include "vulkan_base/VulkanBuffer.hpp"
#include "vulkan_base/VulkanDevice.hpp"

using namespace Kataglyphis;

Mesh::Mesh() = default;

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
  : vertex_count(static_cast<uint32_t>(vertices.size())), index_count(static_cast<uint32_t>(indices.size())),
    device(device)
{
    // glm uses column major matrices so transpose it for Vulkan want row major
    // here
    glm::mat4 transpose_transform = glm::transpose(glm::mat4(1.0F));
    VkTransformMatrixKHR out_matrix;
    std::memcpy(&out_matrix, &transpose_transform, sizeof(VkTransformMatrixKHR));


    object_description = ObjectDescription{};
    createVertexBuffer(transfer_queue, transfer_command_pool, vertices);
    createIndexBuffer(transfer_queue, transfer_command_pool, indices);
    createMaterialIDBuffer(transfer_queue, transfer_command_pool, materialIndex);
    createMaterialBuffer(transfer_queue, transfer_command_pool, materials);

    if (device->supportsBufferDeviceAddress()) {
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
    }

    model = glm::mat4(1.0F);
}

void Mesh::setModel(glm::mat4 new_model) { model = new_model; }

Mesh::~Mesh() = default;

void Mesh::createVertexBuffer(VkQueue /*transfer_queue*/,
  VkCommandPool transfer_command_pool,
  std::vector<Vertex> &vertices)
{
    VkBufferUsageFlags usage_flags =
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkMemoryPropertyFlags const memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateFlags memory_allocate_flags = 0;

    if (device->supportsBufferDeviceAddress()) {
      usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      if (device->supportsHardwareAcceleratedRRT()) {
        usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      }
        memory_allocate_flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(
      device, transfer_command_pool, vertexBuffer, usage_flags, memory_property_flags, vertices, memory_allocate_flags);
}

void Mesh::createIndexBuffer(VkQueue /*transfer_queue*/,
  VkCommandPool transfer_command_pool,
  std::vector<uint32_t> &indices)
{
    VkBufferUsageFlags usage_flags =
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkMemoryPropertyFlags const memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateFlags memory_allocate_flags = 0;

    if (device->supportsBufferDeviceAddress()) {
      usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      if (device->supportsHardwareAcceleratedRRT()) {
        usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      }
        memory_allocate_flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(
      device, transfer_command_pool, indexBuffer, usage_flags, memory_property_flags, indices, memory_allocate_flags);
}

void Mesh::createMaterialIDBuffer(VkQueue /*transfer_queue*/,
  VkCommandPool transfer_command_pool,
  std::vector<unsigned int> &materialIndex)
{
    VkBufferUsageFlags usage_flags =
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkMemoryPropertyFlags const memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateFlags memory_allocate_flags = 0;

    if (device->supportsBufferDeviceAddress()) {
      usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      if (device->supportsHardwareAcceleratedRRT()) {
        usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      }
        memory_allocate_flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      materialIdsBuffer,
      usage_flags,
      memory_property_flags,
      materialIndex,
      memory_allocate_flags);
}

void Mesh::createMaterialBuffer(VkQueue /*transfer_queue*/,
  VkCommandPool transfer_command_pool,
  std::vector<ObjMaterial> &materials)
{
    VkBufferUsageFlags usage_flags =
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkMemoryPropertyFlags const memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateFlags memory_allocate_flags = 0;

    if (device->supportsBufferDeviceAddress()) {
      usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      if (device->supportsHardwareAcceleratedRRT()) {
        usage_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      }
        memory_allocate_flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      materialsBuffer,
      usage_flags,
      memory_property_flags,
      materials,
      memory_allocate_flags);
}
