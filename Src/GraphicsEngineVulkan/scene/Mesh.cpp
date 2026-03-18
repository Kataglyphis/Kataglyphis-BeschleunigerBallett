module;

#include <cstdint>
#include <cstring>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/matrix.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.mesh;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.obj_material;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.vertex;

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
  vk::Queue transfer_queue,
  vk::CommandPool transfer_command_pool,
  std::vector<Vertex> &vertices,
  std::vector<uint32_t> &indices,
  std::vector<unsigned int> &materialIndex,
  std::vector<ObjMaterial> &materials)
  : vertex_count(static_cast<uint32_t>(vertices.size())), index_count(static_cast<uint32_t>(indices.size())),
    device(device)
{
    glm::mat4 transpose_transform = glm::transpose(glm::mat4(1.0F));
    vk::TransformMatrixKHR out_matrix;
    std::memcpy(&out_matrix, &transpose_transform, sizeof(vk::TransformMatrixKHR));


    object_description = ObjectDescription{};
    createVertexBuffer(transfer_queue, transfer_command_pool, vertices);
    createIndexBuffer(transfer_queue, transfer_command_pool, indices);
    createMaterialIDBuffer(transfer_queue, transfer_command_pool, materialIndex);
    createMaterialBuffer(transfer_queue, transfer_command_pool, materials);

    if (device->supportsBufferDeviceAddress()) {
        vk::BufferDeviceAddressInfo vertex_info{};
        vertex_info.buffer = vertexBuffer.getBuffer();

        vk::BufferDeviceAddressInfo index_info{};
        index_info.buffer = indexBuffer.getBuffer();

        vk::BufferDeviceAddressInfo material_index_info{};
        material_index_info.buffer = materialIdsBuffer.getBuffer();

        vk::BufferDeviceAddressInfo material_info{};
        material_info.buffer = materialsBuffer.getBuffer();

        object_description.index_address = device->getLogicalDevice().getBufferDeviceAddress(index_info);
        object_description.vertex_address = device->getLogicalDevice().getBufferDeviceAddress(vertex_info);
        object_description.material_index_address =
          device->getLogicalDevice().getBufferDeviceAddress(material_index_info);
        object_description.material_address = device->getLogicalDevice().getBufferDeviceAddress(material_info);
    }

    model = glm::mat4(1.0F);
}

void Mesh::setModel(glm::mat4 new_model) { model = new_model; }

Mesh::~Mesh() = default;

void Mesh::createVertexBuffer(vk::Queue /*transfer_queue*/,
  vk::CommandPool transfer_command_pool,
  std::vector<Vertex> &vertices)
{
    vk::BufferUsageFlags usage_flags = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer
                                       | vk::BufferUsageFlagBits::eStorageBuffer;
    vk::MemoryPropertyFlags const memory_property_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    vk::MemoryAllocateFlags memory_allocate_flags = {};

    if (device->supportsBufferDeviceAddress()) {
        usage_flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        if (device->supportsHardwareAcceleratedRRT()) {
            usage_flags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        }
        memory_allocate_flags |= vk::MemoryAllocateFlagBits::eDeviceAddress;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(
      device, transfer_command_pool, vertexBuffer, usage_flags, memory_property_flags, vertices, memory_allocate_flags);
}

void Mesh::createIndexBuffer(vk::Queue /*transfer_queue*/,
  vk::CommandPool transfer_command_pool,
  std::vector<uint32_t> &indices)
{
    vk::BufferUsageFlags usage_flags = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer
                                       | vk::BufferUsageFlagBits::eStorageBuffer;
    vk::MemoryPropertyFlags const memory_property_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    vk::MemoryAllocateFlags memory_allocate_flags = {};

    if (device->supportsBufferDeviceAddress()) {
        usage_flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        if (device->supportsHardwareAcceleratedRRT()) {
            usage_flags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        }
        memory_allocate_flags |= vk::MemoryAllocateFlagBits::eDeviceAddress;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(
      device, transfer_command_pool, indexBuffer, usage_flags, memory_property_flags, indices, memory_allocate_flags);
}

void Mesh::createMaterialIDBuffer(vk::Queue /*transfer_queue*/,
  vk::CommandPool transfer_command_pool,
  std::vector<unsigned int> &materialIndex)
{
    vk::BufferUsageFlags usage_flags = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer
                                       | vk::BufferUsageFlagBits::eStorageBuffer;
    vk::MemoryPropertyFlags const memory_property_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    vk::MemoryAllocateFlags memory_allocate_flags = {};

    if (device->supportsBufferDeviceAddress()) {
        usage_flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        if (device->supportsHardwareAcceleratedRRT()) {
            usage_flags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        }
        memory_allocate_flags |= vk::MemoryAllocateFlagBits::eDeviceAddress;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      materialIdsBuffer,
      usage_flags,
      memory_property_flags,
      materialIndex,
      memory_allocate_flags);
}

void Mesh::createMaterialBuffer(vk::Queue /*transfer_queue*/,
  vk::CommandPool transfer_command_pool,
  std::vector<ObjMaterial> &materials)
{
    vk::BufferUsageFlags usage_flags = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer
                                       | vk::BufferUsageFlagBits::eStorageBuffer;
    vk::MemoryPropertyFlags const memory_property_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    vk::MemoryAllocateFlags memory_allocate_flags = {};

    if (device->supportsBufferDeviceAddress()) {
        usage_flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        if (device->supportsHardwareAcceleratedRRT()) {
            usage_flags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        }
        memory_allocate_flags |= vk::MemoryAllocateFlagBits::eDeviceAddress;
    }

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      transfer_command_pool,
      materialsBuffer,
      usage_flags,
      memory_property_flags,
      materials,
      memory_allocate_flags);
}