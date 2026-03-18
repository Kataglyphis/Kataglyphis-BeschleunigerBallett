module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

module kataglyphis.vulkan.as_manager;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.vertex;
import kataglyphis.vulkan.model;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.buffer_manager;

Kataglyphis::VulkanRendererInternals::ASManager::ASManager() = default;

void Kataglyphis::VulkanRendererInternals::ASManager::createASForScene(VulkanDevice *device,
  vk::CommandPool commandPool,
  Kataglyphis::Scene *scene)
{
    this->vulkanDevice = device;
    createBLAS(device, commandPool, scene);
    createTLAS(device, commandPool, scene);
}

void Kataglyphis::VulkanRendererInternals::ASManager::createBLAS(VulkanDevice *device,
  vk::CommandPool commandPool,
  Kataglyphis::Scene *scene)
{
    std::vector<BlasInput> blas_input(scene->getModelCount());

    for (uint32_t model_index = 0; model_index < scene->getModelCount(); model_index++) {
        std::shared_ptr<Model> const mesh_model = scene->get_model_list()[model_index];
        blas_input[model_index].as_geometry.reserve(mesh_model->getMeshCount());
        blas_input[model_index].as_build_offset_info.reserve(mesh_model->getMeshCount());

        for (size_t mesh_index = 0; mesh_index < mesh_model->getMeshCount(); mesh_index++) {
            vk::AccelerationStructureGeometryKHR acceleration_structure_geometry{};
            vk::AccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info{};

            objectToVkGeometryKHR(device,
              mesh_model->getMesh(mesh_index),
              acceleration_structure_geometry,
              acceleration_structure_build_range_info);

            blas_input[model_index].as_geometry.push_back(acceleration_structure_geometry);
            blas_input[model_index].as_build_offset_info.push_back(acceleration_structure_build_range_info);
        }
    }

    std::vector<BuildAccelerationStructure> build_as_structures;
    build_as_structures.resize(scene->getModelCount());

    vk::DeviceSize max_scratch_size = 0;

    for (unsigned int i = 0; i < scene->getModelCount(); i++) {
        vk::DeviceSize current_scretch_size = 0;
        vk::DeviceSize current_size = 0;

        createAccelerationStructureInfosBLAS(
          device, build_as_structures[i], blas_input[i], current_scretch_size, current_size);

        max_scratch_size = std::max(max_scratch_size, current_scretch_size);
    }

    VulkanBuffer scratchBuffer;

    scratchBuffer.create(device,
      max_scratch_size,
      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
        | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      vk::MemoryAllocateFlagBits::eDeviceAddress);

    vk::BufferDeviceAddressInfo scratch_buffer_device_address_info{};
    scratch_buffer_device_address_info.buffer = scratchBuffer.getBuffer();

    vk::DeviceAddress const scratch_buffer_address =
      device->getLogicalDevice().getBufferAddress(scratch_buffer_device_address_info);

    vk::DeviceOrHostAddressKHR scratch_device_or_host_address{};
    scratch_device_or_host_address.deviceAddress = scratch_buffer_address;

    vk::CommandBuffer command_buffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(
      device->getLogicalDevice(), commandPool);

    for (size_t i = 0; i < scene->getModelCount(); i++) {
        createSingleBlas(device, command_buffer, build_as_structures[i], scratch_buffer_address);

        vk::MemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
        barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR;

        command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
          vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
          {},
          barrier,
          {},
          {});
    }

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), command_buffer);

    for (auto &b : build_as_structures) { blas.emplace_back(std::move(b.single_blas)); }

    scratchBuffer.cleanUp();
}

void Kataglyphis::VulkanRendererInternals::ASManager::createTLAS(VulkanDevice *device,
  vk::CommandPool commandPool,
  Kataglyphis::Scene *scene)
{
    std::vector<vk::AccelerationStructureInstanceKHR> tlas_instances;
    tlas_instances.reserve(scene->getModelCount());

    for (size_t model_index = 0; model_index < scene->getModelCount(); model_index++) {
        glm::mat4 transpose_transform = glm::transpose(scene->getModelMatrix(static_cast<int>(model_index)));
        vk::TransformMatrixKHR out_matrix;
        memcpy(&out_matrix, &transpose_transform, sizeof(vk::TransformMatrixKHR));

        vk::AccelerationStructureDeviceAddressInfoKHR acceleration_structure_device_address_info{};
        acceleration_structure_device_address_info.accelerationStructure = blas[model_index].vulkanAS;

        vk::DeviceAddress const acceleration_structure_device_address =
          device->getLogicalDevice().getAccelerationStructureAddressKHR(acceleration_structure_device_address_info);

        vk::AccelerationStructureInstanceKHR geometry_instance{};
        geometry_instance.transform = out_matrix;
        geometry_instance.instanceCustomIndex = model_index;
        geometry_instance.mask = 0xFF;
        geometry_instance.instanceShaderBindingTableRecordOffset = 0;
        geometry_instance.flags = vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
        geometry_instance.accelerationStructureReference = acceleration_structure_device_address;
        geometry_instance.instanceShaderBindingTableRecordOffset = 0;

        tlas_instances.emplace_back(geometry_instance);
    }

    vk::CommandBuffer command_buffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(
      device->getLogicalDevice(), commandPool);

    VulkanBuffer geometryInstanceBuffer;

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      commandPool,
      geometryInstanceBuffer,
      vk::BufferUsageFlagBits::eShaderDeviceAddress
        | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      tlas_instances,
      vk::MemoryAllocateFlagBits::eDeviceAddress);

    vk::BufferDeviceAddressInfo geometry_instance_buffer_device_address_info{};
    geometry_instance_buffer_device_address_info.buffer = geometryInstanceBuffer.getBuffer();

    vk::DeviceAddress const geometry_instance_buffer_address =
      device->getLogicalDevice().getBufferAddress(geometry_instance_buffer_device_address_info);

    vk::MemoryBarrier barrier;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
      {},
      barrier,
      {},
      {});

    vk::AccelerationStructureGeometryInstancesDataKHR acceleration_structure_geometry_instances_data{};
    acceleration_structure_geometry_instances_data.data.deviceAddress = geometry_instance_buffer_address;

    vk::AccelerationStructureGeometryKHR topAS_acceleration_structure_geometry{};
    topAS_acceleration_structure_geometry.geometryType = vk::GeometryTypeKHR::eInstances;
    topAS_acceleration_structure_geometry.geometry.instances = acceleration_structure_geometry_instances_data;

    vk::AccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info{};
    acceleration_structure_build_geometry_info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    acceleration_structure_build_geometry_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    acceleration_structure_build_geometry_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    acceleration_structure_build_geometry_info.srcAccelerationStructure = nullptr;
    acceleration_structure_build_geometry_info.geometryCount = 1;
    acceleration_structure_build_geometry_info.pGeometries = &topAS_acceleration_structure_geometry;

    vk::AccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info{};

    auto const count_instance = static_cast<uint32_t>(tlas_instances.size());
    device->getLogicalDevice().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eHost,
      &acceleration_structure_build_geometry_info,
      &count_instance,
      &acceleration_structure_build_sizes_info);

    VulkanBuffer &tlasVulkanBuffer = tlas.vulkanBuffer;
    tlasVulkanBuffer.create(device,
      acceleration_structure_build_sizes_info.accelerationStructureSize,
      vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress
        | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      vk::MemoryAllocateFlagBits::eDeviceAddress);

    vk::AccelerationStructureCreateInfoKHR acceleration_structure_create_info{};
    acceleration_structure_create_info.createFlags = {};
    acceleration_structure_create_info.buffer = tlasVulkanBuffer.getBuffer();
    acceleration_structure_create_info.offset = 0;
    acceleration_structure_create_info.size = acceleration_structure_build_sizes_info.accelerationStructureSize;
    acceleration_structure_create_info.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    acceleration_structure_create_info.deviceAddress = 0;

    vk::AccelerationStructureKHR &tlAS = tlas.vulkanAS;
    tlAS = device->getLogicalDevice().createAccelerationStructureKHR(acceleration_structure_create_info);

    VulkanBuffer scratchBuffer;

    scratchBuffer.create(device,
      acceleration_structure_build_sizes_info.buildScratchSize,
      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
        | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      vk::MemoryAllocateFlagBits::eDeviceAddress);

    vk::BufferDeviceAddressInfo scratch_buffer_device_address_info{};
    scratch_buffer_device_address_info.buffer = scratchBuffer.getBuffer();

    vk::DeviceAddress const scratch_buffer_address =
      device->getLogicalDevice().getBufferAddress(scratch_buffer_device_address_info);

    acceleration_structure_build_geometry_info.scratchData.deviceAddress = scratch_buffer_address;
    acceleration_structure_build_geometry_info.srcAccelerationStructure = nullptr;
    acceleration_structure_build_geometry_info.dstAccelerationStructure = tlAS;

    vk::AccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info{};
    acceleration_structure_build_range_info.primitiveCount = scene->getModelCount();
    acceleration_structure_build_range_info.primitiveOffset = 0;
    acceleration_structure_build_range_info.firstVertex = 0;
    acceleration_structure_build_range_info.transformOffset = 0;

    vk::AccelerationStructureBuildRangeInfoKHR const *acceleration_structure_build_range_infos =
      &acceleration_structure_build_range_info;

    command_buffer.buildAccelerationStructuresKHR(
      1, &acceleration_structure_build_geometry_info, &acceleration_structure_build_range_infos);

    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), command_buffer);
    scratchBuffer.cleanUp();
    geometryInstanceBuffer.cleanUp();
}

void Kataglyphis::VulkanRendererInternals::ASManager::cleanUp()
{
    device->getLogicalDevice().destroyAccelerationStructureKHR(tlas.vulkanAS);

    tlas.vulkanBuffer.cleanUp();

    for (auto &bla : blas) {
        device->getLogicalDevice().destroyAccelerationStructureKHR(bla.vulkanAS);

        bla.vulkanBuffer.cleanUp();
    }
}

Kataglyphis::VulkanRendererInternals::ASManager::~ASManager() = default;

void Kataglyphis::VulkanRendererInternals::ASManager::createSingleBlas(VulkanDevice *device,
  vk::CommandBuffer command_buffer,
  BuildAccelerationStructure &build_as_structure,
  vk::DeviceAddress scratch_device_or_host_address)
{
    vk::AccelerationStructureCreateInfoKHR acceleration_structure_create_info{};
    acceleration_structure_create_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    acceleration_structure_create_info.size = build_as_structure.size_info.accelerationStructureSize;
    VulkanBuffer &blasVulkanBuffer = build_as_structure.single_blas.vulkanBuffer;
    blasVulkanBuffer.create(device,
      build_as_structure.size_info.accelerationStructureSize,
      vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress
        | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      vk::MemoryAllocateFlagBits::eDeviceAddress);

    acceleration_structure_create_info.buffer = blasVulkanBuffer.getBuffer();
    vk::AccelerationStructureKHR &blas_as = build_as_structure.single_blas.vulkanAS;
    blas_as = device->getLogicalDevice().createAccelerationStructureKHR(acceleration_structure_create_info);

    build_as_structure.build_info.dstAccelerationStructure = blas_as;
    build_as_structure.build_info.scratchData.deviceAddress = scratch_device_or_host_address;

    command_buffer.buildAccelerationStructuresKHR(1, &build_as_structure.build_info, &build_as_structure.range_info);
}

void Kataglyphis::VulkanRendererInternals::ASManager::createAccelerationStructureInfosBLAS(VulkanDevice *device,
  BuildAccelerationStructure &build_as_structure,
  BlasInput &blas_input,
  vk::DeviceSize &current_scretch_size,
  vk::DeviceSize &current_size)
{
    build_as_structure.build_info.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    build_as_structure.build_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    build_as_structure.build_info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    build_as_structure.build_info.geometryCount = static_cast<uint32_t>(blas_input.as_geometry.size());
    build_as_structure.build_info.pGeometries = blas_input.as_geometry.data();

    build_as_structure.range_info = blas_input.as_build_offset_info.data();

    std::vector<uint32_t> max_primitive_cnt(blas_input.as_build_offset_info.size());

    for (uint32_t temp = 0; temp < static_cast<uint32_t>(blas_input.as_build_offset_info.size()); temp++) {
        max_primitive_cnt[temp] = blas_input.as_build_offset_info[temp].primitiveCount;
    }

    device->getLogicalDevice().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice,
      &build_as_structure.build_info,
      max_primitive_cnt.data(),
      &build_as_structure.size_info);

    current_size = build_as_structure.size_info.accelerationStructureSize;
    current_scretch_size = build_as_structure.size_info.buildScratchSize;
}

void Kataglyphis::VulkanRendererInternals::ASManager::objectToVkGeometryKHR(VulkanDevice *device,
  Kataglyphis::Mesh *mesh,
  vk::AccelerationStructureGeometryKHR &acceleration_structure_geometry,
  vk::AccelerationStructureBuildRangeInfoKHR &acceleration_structure_build_range_info)
{
    vk::BufferDeviceAddressInfo vertex_buffer_device_address_info{};
    vertex_buffer_device_address_info.buffer = mesh->getVertexBuffer();

    vk::BufferDeviceAddressInfo index_buffer_device_address_info{};
    index_buffer_device_address_info.buffer = mesh->getIndexBuffer();

    vk::DeviceAddress const vertex_buffer_address =
      device->getLogicalDevice().getBufferAddress(vertex_buffer_device_address_info);
    vk::DeviceAddress const index_buffer_address =
      device->getLogicalDevice().getBufferAddress(index_buffer_device_address_info);

    vk::DeviceOrHostAddressConstKHR vertex_device_or_host_address_const{};
    vertex_device_or_host_address_const.deviceAddress = vertex_buffer_address;

    vk::DeviceOrHostAddressConstKHR index_device_or_host_address_const{};
    index_device_or_host_address_const.deviceAddress = index_buffer_address;

    vk::AccelerationStructureGeometryTrianglesDataKHR acceleration_structure_triangles_data{};
    acceleration_structure_triangles_data.vertexFormat = vk::Format::eR32G32B32Sfloat;
    acceleration_structure_triangles_data.vertexData = vertex_device_or_host_address_const;
    acceleration_structure_triangles_data.vertexStride = sizeof(Vertex);
    acceleration_structure_triangles_data.maxVertex = mesh->getVertexCount();
    acceleration_structure_triangles_data.indexType = vk::IndexType::eUint32;
    acceleration_structure_triangles_data.indexData = index_device_or_host_address_const;

    vk::AccelerationStructureGeometryDataKHR acceleration_structure_geometry_data{};
    acceleration_structure_geometry_data.triangles = acceleration_structure_triangles_data;

    acceleration_structure_geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
    acceleration_structure_geometry.geometry = acceleration_structure_geometry_data;
    acceleration_structure_geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

    acceleration_structure_build_range_info.primitiveCount = mesh->getIndexCount() / 3;
    acceleration_structure_build_range_info.primitiveOffset = 0;
    acceleration_structure_build_range_info.firstVertex = 0;
    acceleration_structure_build_range_info.transformOffset = 0;
}