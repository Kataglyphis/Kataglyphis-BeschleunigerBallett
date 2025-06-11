#include "renderer/accelerationStructures/ASManager.hpp"

Kataglyphis::VulkanRendererInternals::ASManager::ASManager() {}

void Kataglyphis::VulkanRendererInternals::ASManager::createASForScene(VulkanDevice *device,
  VkCommandPool commandPool,
  Scene *scene)
{
    this->vulkanDevice = device;
    createBLAS(device, commandPool, scene);
    createTLAS(device, commandPool, scene);
}

void Kataglyphis::VulkanRendererInternals::ASManager::createBLAS(VulkanDevice *device,
  VkCommandPool commandPool,
  Scene *scene)
{
    // LOAD ALL NECESSARY FUNCTIONS STRAIGHT IN THE BEGINNING
    // all functionality from extensions has to be loaded in the beginning
    // we need a reference to the device location of our geometry laying on the
    // graphics card we already uploaded objects and created vertex and index
    // buffers respectively

    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR =
      (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device->getLogicalDevice(), "vkGetBufferDeviceAddress");

    std::vector<BlasInput> blas_input(scene->getModelCount());

    for (uint32_t model_index = 0; model_index < static_cast<uint32_t>(scene->getModelCount()); model_index++) {
        std::shared_ptr<Model> mesh_model = scene->get_model_list()[model_index];
        // blas_input.emplace_back();
        blas_input[model_index].as_geometry.reserve(mesh_model->getMeshCount());
        blas_input[model_index].as_build_offset_info.reserve(mesh_model->getMeshCount());

        for (size_t mesh_index = 0; mesh_index < mesh_model->getMeshCount(); mesh_index++) {
            VkAccelerationStructureGeometryKHR acceleration_structure_geometry{};
            VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info{};

            objectToVkGeometryKHR(device,
              mesh_model->getMesh(mesh_index),
              acceleration_structure_geometry,
              acceleration_structure_build_range_info);
            // this only specifies the acceleration structure
            // we are building it in the end for the whole model with the build
            // command

            blas_input[model_index].as_geometry.push_back(acceleration_structure_geometry);
            blas_input[model_index].as_build_offset_info.push_back(acceleration_structure_build_range_info);
        }
    }

    std::vector<BuildAccelerationStructure> build_as_structures;
    build_as_structures.resize(scene->getModelCount());

    VkDeviceSize max_scratch_size = 0;
    VkDeviceSize total_size_all_BLAS = 0;

    for (unsigned int i = 0; i < scene->getModelCount(); i++) {
        VkDeviceSize current_scretch_size = 0;
        VkDeviceSize current_size = 0;

        createAccelerationStructureInfosBLAS(
          device, build_as_structures[i], blas_input[i], current_scretch_size, current_size);

        total_size_all_BLAS += current_size;
        max_scratch_size = std::max(max_scratch_size, current_scretch_size);
    }

    VulkanBuffer scratchBuffer;

    scratchBuffer.create(device,
      max_scratch_size,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkBufferDeviceAddressInfo scratch_buffer_device_address_info{};
    scratch_buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratch_buffer_device_address_info.buffer = scratchBuffer.getBuffer();

    VkDeviceAddress scratch_buffer_address =
      pvkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &scratch_buffer_device_address_info);

    VkDeviceOrHostAddressKHR scratch_device_or_host_address{};
    scratch_device_or_host_address.deviceAddress = scratch_buffer_address;

    VkCommandBuffer command_buffer = commandBufferManager.beginCommandBuffer(device->getLogicalDevice(), commandPool);

    for (size_t i = 0; i < scene->getModelCount(); i++) {
        createSingleBlas(device, command_buffer, build_as_structures[i], scratch_buffer_address);

        VkMemoryBarrier barrier;
        barrier.pNext = nullptr;
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        vkCmdPipelineBarrier(command_buffer,
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
          0,
          1,
          &barrier,
          0,
          nullptr,
          0,
          nullptr);
    }

    commandBufferManager.endAndSubmitCommandBuffer(
      device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), command_buffer);

    for (auto &b : build_as_structures) { blas.emplace_back(b.single_blas); }

    scratchBuffer.cleanUp();
}

void Kataglyphis::VulkanRendererInternals::ASManager::createTLAS(VulkanDevice *device,
  VkCommandPool commandPool,
  Scene *scene)
{
    // LOAD ALL NECESSARY FUNCTIONS STRAIGHT IN THE BEGINNING
    // all functionality from extensions has to be loaded in the beginning
    // we need a reference to the device location of our geometry laying on the
    // graphics card we already uploaded objects and created vertex and index
    // buffers respectively
    PFN_vkGetAccelerationStructureBuildSizesKHR pvkGetAccelerationStructureBuildSizesKHR =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkGetAccelerationStructureBuildSizesKHR");

    PFN_vkCreateAccelerationStructureKHR pvkCreateAccelerationStructureKHR =
      (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkCreateAccelerationStructureKHR");

    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR =
      (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device->getLogicalDevice(), "vkGetBufferDeviceAddress");

    PFN_vkCmdBuildAccelerationStructuresKHR pvkCmdBuildAccelerationStructuresKHR =
      (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkCmdBuildAccelerationStructuresKHR");

    PFN_vkGetAccelerationStructureDeviceAddressKHR pvkGetAccelerationStructureDeviceAddressKHR =
      (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkGetAccelerationStructureDeviceAddressKHR");

    std::vector<VkAccelerationStructureInstanceKHR> tlas_instances;
    tlas_instances.reserve(scene->getModelCount());

    for (size_t model_index = 0; model_index < scene->getModelCount(); model_index++) {
        // glm uses column major matrices so transpose it for Vulkan want row major
        // here
        glm::mat4 transpose_transform = glm::transpose(scene->getModelMatrix(static_cast<int>(model_index)));
        VkTransformMatrixKHR out_matrix;
        memcpy(&out_matrix, &transpose_transform, sizeof(VkTransformMatrixKHR));

        VkAccelerationStructureDeviceAddressInfoKHR acceleration_structure_device_address_info{};
        acceleration_structure_device_address_info.sType =
          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        acceleration_structure_device_address_info.accelerationStructure = blas[model_index].vulkanAS;

        VkDeviceAddress acceleration_structure_device_address = pvkGetAccelerationStructureDeviceAddressKHR(
          device->getLogicalDevice(), &acceleration_structure_device_address_info);

        VkAccelerationStructureInstanceKHR geometry_instance{};
        geometry_instance.transform = out_matrix;
        geometry_instance.instanceCustomIndex = model_index;// gl_InstanceCustomIndexEXT
        geometry_instance.mask = 0xFF;
        geometry_instance.instanceShaderBindingTableRecordOffset = 0;
        geometry_instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        geometry_instance.accelerationStructureReference = acceleration_structure_device_address;
        geometry_instance.instanceShaderBindingTableRecordOffset = 0;// same hit group for all objects

        tlas_instances.emplace_back(geometry_instance);
    }

    VkCommandBuffer command_buffer = commandBufferManager.beginCommandBuffer(device->getLogicalDevice(), commandPool);

    VulkanBuffer geometryInstanceBuffer;

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
      commandPool,
      geometryInstanceBuffer,
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      tlas_instances);

    VkBufferDeviceAddressInfo geometry_instance_buffer_device_address_info{};
    geometry_instance_buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    geometry_instance_buffer_device_address_info.buffer = geometryInstanceBuffer.getBuffer();

    VkDeviceAddress geometry_instance_buffer_address =
      pvkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &geometry_instance_buffer_device_address_info);

    // Make sure the copy of the instance buffer are copied before triggering the
    // acceleration structure build
    VkMemoryBarrier barrier;
    barrier.pNext = nullptr;
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    vkCmdPipelineBarrier(command_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      0,
      1,
      &barrier,
      0,
      nullptr,
      0,
      nullptr);

    VkAccelerationStructureGeometryInstancesDataKHR acceleration_structure_geometry_instances_data{};
    acceleration_structure_geometry_instances_data.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    acceleration_structure_geometry_instances_data.pNext = nullptr;
    acceleration_structure_geometry_instances_data.data.deviceAddress = geometry_instance_buffer_address;

    VkAccelerationStructureGeometryKHR topAS_acceleration_structure_geometry{};
    topAS_acceleration_structure_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    topAS_acceleration_structure_geometry.pNext = nullptr;
    topAS_acceleration_structure_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    topAS_acceleration_structure_geometry.geometry.instances = acceleration_structure_geometry_instances_data;

    // find sizes
    VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info{};
    acceleration_structure_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    acceleration_structure_build_geometry_info.pNext = nullptr;
    acceleration_structure_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    acceleration_structure_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    acceleration_structure_build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    acceleration_structure_build_geometry_info.srcAccelerationStructure = VK_NULL_HANDLE;
    acceleration_structure_build_geometry_info.geometryCount = 1;
    acceleration_structure_build_geometry_info.pGeometries = &topAS_acceleration_structure_geometry;

    VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info{};
    acceleration_structure_build_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    acceleration_structure_build_sizes_info.pNext = nullptr;
    acceleration_structure_build_sizes_info.accelerationStructureSize = 0;
    acceleration_structure_build_sizes_info.updateScratchSize = 0;
    acceleration_structure_build_sizes_info.buildScratchSize = 0;

    uint32_t count_instance = static_cast<uint32_t>(tlas_instances.size());
    pvkGetAccelerationStructureBuildSizesKHR(device->getLogicalDevice(),
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR,
      &acceleration_structure_build_geometry_info,
      &count_instance,
      &acceleration_structure_build_sizes_info);

    // now we got the sizes
    VulkanBuffer &tlasVulkanBuffer = tlas.vulkanBuffer;
    tlasVulkanBuffer.create(device,
      acceleration_structure_build_sizes_info.accelerationStructureSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR acceleration_structure_create_info{};
    acceleration_structure_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    acceleration_structure_create_info.pNext = nullptr;
    acceleration_structure_create_info.createFlags = 0;
    acceleration_structure_create_info.buffer = tlasVulkanBuffer.getBuffer();
    acceleration_structure_create_info.offset = 0;
    acceleration_structure_create_info.size = acceleration_structure_build_sizes_info.accelerationStructureSize;
    acceleration_structure_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    acceleration_structure_create_info.deviceAddress = 0;

    VkAccelerationStructureKHR &tlAS = tlas.vulkanAS;
    pvkCreateAccelerationStructureKHR(device->getLogicalDevice(), &acceleration_structure_create_info, nullptr, &tlAS);

    VulkanBuffer scratchBuffer;

    scratchBuffer.create(device,
      acceleration_structure_build_sizes_info.buildScratchSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    VkBufferDeviceAddressInfo scratch_buffer_device_address_info{};
    scratch_buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratch_buffer_device_address_info.buffer = scratchBuffer.getBuffer();

    VkDeviceAddress scratch_buffer_address =
      pvkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &scratch_buffer_device_address_info);

    // update build info
    acceleration_structure_build_geometry_info.scratchData.deviceAddress = scratch_buffer_address;
    acceleration_structure_build_geometry_info.srcAccelerationStructure = VK_NULL_HANDLE;
    acceleration_structure_build_geometry_info.dstAccelerationStructure = tlAS;

    VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info{};
    acceleration_structure_build_range_info.primitiveCount = scene->getModelCount();
    acceleration_structure_build_range_info.primitiveOffset = 0;
    acceleration_structure_build_range_info.firstVertex = 0;
    acceleration_structure_build_range_info.transformOffset = 0;

    VkAccelerationStructureBuildRangeInfoKHR *acceleration_structure_build_range_infos =
      &acceleration_structure_build_range_info;

    pvkCmdBuildAccelerationStructuresKHR(
      command_buffer, 1, &acceleration_structure_build_geometry_info, &acceleration_structure_build_range_infos);

    commandBufferManager.endAndSubmitCommandBuffer(
      device->getLogicalDevice(), commandPool, device->getGraphicsQueue(), command_buffer);
    scratchBuffer.cleanUp();
    geometryInstanceBuffer.cleanUp();
}

void Kataglyphis::VulkanRendererInternals::ASManager::cleanUp()
{
    PFN_vkDestroyAccelerationStructureKHR pvkDestroyAccelerationStructureKHR =
      (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
        vulkanDevice->getLogicalDevice(), "vkDestroyAccelerationStructureKHR");

    pvkDestroyAccelerationStructureKHR(vulkanDevice->getLogicalDevice(), tlas.vulkanAS, nullptr);

    tlas.vulkanBuffer.cleanUp();

    for (size_t index = 0; index < blas.size(); index++) {
        pvkDestroyAccelerationStructureKHR(vulkanDevice->getLogicalDevice(), blas[index].vulkanAS, nullptr);

        blas[index].vulkanBuffer.cleanUp();
    }
}

Kataglyphis::VulkanRendererInternals::ASManager::~ASManager() {}

void Kataglyphis::VulkanRendererInternals::ASManager::createSingleBlas(VulkanDevice *device,
  VkCommandBuffer command_buffer,
  BuildAccelerationStructure &build_as_structure,
  VkDeviceAddress scratch_device_or_host_address)
{
    PFN_vkCreateAccelerationStructureKHR pvkCreateAccelerationStructureKHR =
      (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkCreateAccelerationStructureKHR");

    PFN_vkCmdBuildAccelerationStructuresKHR pvkCmdBuildAccelerationStructuresKHR =
      (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkCmdBuildAccelerationStructuresKHR");

    VkAccelerationStructureCreateInfoKHR acceleration_structure_create_info{};
    acceleration_structure_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    acceleration_structure_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    acceleration_structure_create_info.size = build_as_structure.size_info.accelerationStructureSize;
    VulkanBuffer &blasVulkanBuffer = build_as_structure.single_blas.vulkanBuffer;
    blasVulkanBuffer.create(device,
      build_as_structure.size_info.accelerationStructureSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    acceleration_structure_create_info.buffer = blasVulkanBuffer.getBuffer();
    VkAccelerationStructureKHR &blas_as = build_as_structure.single_blas.vulkanAS;
    pvkCreateAccelerationStructureKHR(
      device->getLogicalDevice(), &acceleration_structure_create_info, nullptr, &blas_as);

    build_as_structure.build_info.dstAccelerationStructure = blas_as;
    build_as_structure.build_info.scratchData.deviceAddress = scratch_device_or_host_address;

    pvkCmdBuildAccelerationStructuresKHR(
      command_buffer, 1, &build_as_structure.build_info, &build_as_structure.range_info);
}

void Kataglyphis::VulkanRendererInternals::ASManager::createAccelerationStructureInfosBLAS(VulkanDevice *device,
  BuildAccelerationStructure &build_as_structure,
  BlasInput &blas_input,
  VkDeviceSize &current_scretch_size,
  VkDeviceSize &current_size)
{
    PFN_vkGetAccelerationStructureBuildSizesKHR pvkGetAccelerationStructureBuildSizesKHR =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkGetAccelerationStructureBuildSizesKHR");

    build_as_structure.build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_as_structure.build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build_as_structure.build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_as_structure.build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_as_structure.build_info.geometryCount = static_cast<uint32_t>(blas_input.as_geometry.size());
    build_as_structure.build_info.pGeometries = blas_input.as_geometry.data();

    build_as_structure.range_info = blas_input.as_build_offset_info.data();

    build_as_structure.size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    std::vector<uint32_t> max_primitive_cnt(blas_input.as_build_offset_info.size());

    for (uint32_t temp = 0; temp < static_cast<uint32_t>(blas_input.as_build_offset_info.size()); temp++)
        max_primitive_cnt[temp] = blas_input.as_build_offset_info[temp].primitiveCount;

    pvkGetAccelerationStructureBuildSizesKHR(device->getLogicalDevice(),
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &build_as_structure.build_info,
      max_primitive_cnt.data(),
      &build_as_structure.size_info);

    current_size = build_as_structure.size_info.accelerationStructureSize;
    current_scretch_size = build_as_structure.size_info.buildScratchSize;
}

void Kataglyphis::VulkanRendererInternals::ASManager::objectToVkGeometryKHR(VulkanDevice *device,
  Mesh *mesh,
  VkAccelerationStructureGeometryKHR &acceleration_structure_geometry,
  VkAccelerationStructureBuildRangeInfoKHR &acceleration_structure_build_range_info)
{
    // LOAD ALL NECESSARY FUNCTIONS STRAIGHT IN THE BEGINNING
    // all functionality from extensions has to be loaded in the beginning
    // we need a reference to the device location of our geometry laying on the
    // graphics card we already uploaded objects and created vertex and index
    // buffers respectively
    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR =
      (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device->getLogicalDevice(), "vkGetBufferDeviceAddress");

    // all starts with the address of our vertex and index data we already
    // uploaded in buffers earlier when loading the meshes/models
    VkBufferDeviceAddressInfo vertex_buffer_device_address_info{};
    vertex_buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    vertex_buffer_device_address_info.buffer = mesh->getVertexBuffer();
    vertex_buffer_device_address_info.pNext = nullptr;

    VkBufferDeviceAddressInfo index_buffer_device_address_info{};
    index_buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    index_buffer_device_address_info.buffer = mesh->getIndexBuffer();
    index_buffer_device_address_info.pNext = nullptr;

    // receiving address to move on
    VkDeviceAddress vertex_buffer_address =
      pvkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &vertex_buffer_device_address_info);
    VkDeviceAddress index_buffer_address =
      pvkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &index_buffer_device_address_info);

    // convert to const address for further processing
    VkDeviceOrHostAddressConstKHR vertex_device_or_host_address_const{};
    vertex_device_or_host_address_const.deviceAddress = vertex_buffer_address;

    VkDeviceOrHostAddressConstKHR index_device_or_host_address_const{};
    index_device_or_host_address_const.deviceAddress = index_buffer_address;

    VkAccelerationStructureGeometryTrianglesDataKHR acceleration_structure_triangles_data{};
    acceleration_structure_triangles_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    acceleration_structure_triangles_data.pNext = nullptr;
    acceleration_structure_triangles_data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    acceleration_structure_triangles_data.vertexData = vertex_device_or_host_address_const;
    acceleration_structure_triangles_data.vertexStride = sizeof(Vertex);
    acceleration_structure_triangles_data.maxVertex = mesh->getVertexCount();
    acceleration_structure_triangles_data.indexType = VK_INDEX_TYPE_UINT32;
    acceleration_structure_triangles_data.indexData = index_device_or_host_address_const;

    // can also be instances or AABBs; not covered here
    // but to identify as triangles put it ito these struct
    VkAccelerationStructureGeometryDataKHR acceleration_structure_geometry_data{};
    acceleration_structure_geometry_data.triangles = acceleration_structure_triangles_data;

    acceleration_structure_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    acceleration_structure_geometry.pNext = nullptr;
    acceleration_structure_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    acceleration_structure_geometry.geometry = acceleration_structure_geometry_data;
    acceleration_structure_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    // we have triangles so divide the number of vertices with 3!!
    // for our simple case a no brainer
    // take entire data to build BLAS
    // number of indices is truly the stick point here
    acceleration_structure_build_range_info.primitiveCount = mesh->getIndexCount() / 3;
    acceleration_structure_build_range_info.primitiveOffset = 0;
    acceleration_structure_build_range_info.firstVertex = 0;
    acceleration_structure_build_range_info.transformOffset = 0;
}
