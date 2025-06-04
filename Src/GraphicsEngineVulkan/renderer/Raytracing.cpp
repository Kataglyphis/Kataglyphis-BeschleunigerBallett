#include "Raytracing.hpp"

#include <array>
#include <filesystem>
#include <sstream>
#include <vector>

#include "common/MemoryHelper.hpp"
#include "common/Utilities.hpp"
#include "renderer/VulkanRendererConfig.hpp"
#include "util/File.hpp"
#include "vulkan_base/ShaderHelper.hpp"

KataglyphisRenderer::VulkanRendererInternals::Raytracing::Raytracing() {}

void KataglyphisRenderer::VulkanRendererInternals::Raytracing::init(VulkanDevice *device,
  const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
{
    this->device = device;

    createPCRange();
    createGraphicsPipeline(descriptorSetLayouts);
    createSBT();
}

void KataglyphisRenderer::VulkanRendererInternals::Raytracing::shaderHotReload(
  const std::vector<VkDescriptorSetLayout> &descriptor_set_layouts)
{
    vkDestroyPipeline(device->getLogicalDevice(), graphicsPipeline, nullptr);
    createGraphicsPipeline(descriptor_set_layouts);
}

void KataglyphisRenderer::VulkanRendererInternals::Raytracing::recordCommands(VkCommandBuffer &commandBuffer,
  VulkanSwapChain *vulkanSwapChain,
  const std::vector<VkDescriptorSet> &descriptorSets)
{
    uint32_t handle_size = raytracing_properties.shaderGroupHandleSize;
    uint32_t handle_size_aligned = align_up(handle_size, raytracing_properties.shaderGroupHandleAlignment);

    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
      vkGetDeviceProcAddr(device->getLogicalDevice(), "vkGetBufferDeviceAddressKHR"));

    PFN_vkCmdTraceRaysKHR pvkCmdTraceRaysKHR =
      (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(device->getLogicalDevice(), "vkCmdTraceRaysKHR");

    VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
    bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAI.buffer = raygenShaderBindingTableBuffer.getBuffer();

    rgen_region.deviceAddress = vkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &bufferDeviceAI);
    rgen_region.stride = handle_size_aligned;
    rgen_region.size = handle_size_aligned;

    bufferDeviceAI.buffer = missShaderBindingTableBuffer.getBuffer();
    miss_region.deviceAddress = vkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &bufferDeviceAI);
    miss_region.stride = handle_size_aligned;
    miss_region.size = handle_size_aligned;

    bufferDeviceAI.buffer = hitShaderBindingTableBuffer.getBuffer();
    hit_region.deviceAddress = vkGetBufferDeviceAddressKHR(device->getLogicalDevice(), &bufferDeviceAI);
    hit_region.stride = handle_size_aligned;
    hit_region.size = handle_size_aligned;

    // for GCC doen't allow references on rvalues go like that ...
    pc.clear_color = { 0.2f, 0.65f, 0.4f, 1.0f };
    // just "Push" constants to given shader stage directly (no buffer)
    vkCmdPushConstants(commandBuffer,
      pipeline_layout,
      VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
      0,
      sizeof(PushConstantRaytracing),
      &pc);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, graphicsPipeline);

    vkCmdBindDescriptorSets(commandBuffer,
      VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
      pipeline_layout,
      0,
      static_cast<uint32_t>(descriptorSets.size()),
      descriptorSets.data(),
      0,
      nullptr);

    const VkExtent2D &swap_chain_extent = vulkanSwapChain->getSwapChainExtent();
    pvkCmdTraceRaysKHR(commandBuffer,
      &rgen_region,
      &miss_region,
      &hit_region,
      &call_region,
      swap_chain_extent.width,
      swap_chain_extent.height,
      1);
}

void KataglyphisRenderer::VulkanRendererInternals::Raytracing::cleanUp()
{
    shaderBindingTableBuffer.cleanUp();
    raygenShaderBindingTableBuffer.cleanUp();
    missShaderBindingTableBuffer.cleanUp();
    hitShaderBindingTableBuffer.cleanUp();

    vkDestroyPipeline(device->getLogicalDevice(), graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device->getLogicalDevice(), pipeline_layout, nullptr);
}

KataglyphisRenderer::VulkanRendererInternals::Raytracing::~Raytracing() {}

void KataglyphisRenderer::VulkanRendererInternals::Raytracing::createPCRange()
{
    // define push constant values (no 'create' needed)
    pc_ranges.stageFlags =
      VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
    pc_ranges.offset = 0;
    pc_ranges.size = sizeof(PushConstantRaytracing);// size of data being passed
}

void KataglyphisRenderer::VulkanRendererInternals::Raytracing::createGraphicsPipeline(
  const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
{
    PFN_vkCreateRayTracingPipelinesKHR pvkCreateRayTracingPipelinesKHR =
      (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkCreateRayTracingPipelinesKHR");

    std::stringstream raytracing_shader_dir;
    std::filesystem::path cwd = std::filesystem::current_path();
    raytracing_shader_dir << cwd.string();
    raytracing_shader_dir << RELATIVE_RESOURCE_PATH;
    raytracing_shader_dir << "Shaders/raytracing/";

    std::string raygen_shader = "raytrace.rgen";
    std::string chit_shader = "raytrace.rchit";
    std::string miss_shader = "raytrace.rmiss";
    std::string shadow_shader = "shadow.rmiss";

    ShaderHelper shaderHelper;
    shaderHelper.compileShader(raytracing_shader_dir.str(), raygen_shader);
    shaderHelper.compileShader(raytracing_shader_dir.str(), chit_shader);
    shaderHelper.compileShader(raytracing_shader_dir.str(), miss_shader);
    shaderHelper.compileShader(raytracing_shader_dir.str(), shadow_shader);

    File raygenFile(shaderHelper.getShaderSpvDir(raytracing_shader_dir.str(), raygen_shader));
    File raychitFile(shaderHelper.getShaderSpvDir(raytracing_shader_dir.str(), chit_shader));
    File raymissFile(shaderHelper.getShaderSpvDir(raytracing_shader_dir.str(), miss_shader));
    File shadowFile(shaderHelper.getShaderSpvDir(raytracing_shader_dir.str(), shadow_shader));

    std::vector<char> raygen_shader_code = raygenFile.readCharSequence();
    std::vector<char> raychit_shader_code = raychitFile.readCharSequence();
    std::vector<char> raymiss_shader_code = raymissFile.readCharSequence();
    std::vector<char> shadow_shader_code = shadowFile.readCharSequence();

    // build shader modules to link to graphics pipeline
    VkShaderModule raygen_shader_module = shaderHelper.createShaderModule(device, raygen_shader_code);
    VkShaderModule raychit_shader_module = shaderHelper.createShaderModule(device, raychit_shader_code);
    VkShaderModule raymiss_shader_module = shaderHelper.createShaderModule(device, raymiss_shader_code);
    VkShaderModule shadow_shader_module = shaderHelper.createShaderModule(device, shadow_shader_code);

    // create all shader stage infos for creating a group
    VkPipelineShaderStageCreateInfo rgen_shader_stage_info{};
    rgen_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    rgen_shader_stage_info.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    rgen_shader_stage_info.module = raygen_shader_module;
    rgen_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo rmiss_shader_stage_info{};
    rmiss_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    rmiss_shader_stage_info.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    rmiss_shader_stage_info.module = raymiss_shader_module;
    rmiss_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shadow_shader_stage_info{};
    shadow_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadow_shader_stage_info.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    shadow_shader_stage_info.module = shadow_shader_module;
    shadow_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo rchit_shader_stage_info{};
    rchit_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    rchit_shader_stage_info.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    rchit_shader_stage_info.module = raychit_shader_module;
    rchit_shader_stage_info.pName = "main";

    // we have all shader stages together
    std::array<VkPipelineShaderStageCreateInfo, 4> shader_stages = {
        rgen_shader_stage_info, rmiss_shader_stage_info, shadow_shader_stage_info, rchit_shader_stage_info
    };

    enum StageIndices { eRaygen, eMiss, eMiss2, eClosestHit, eShaderGroupCount };

    shader_groups.reserve(4);
    VkRayTracingShaderGroupCreateInfoKHR shader_group_create_infos[4];

    shader_group_create_infos[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shader_group_create_infos[0].pNext = nullptr;
    shader_group_create_infos[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shader_group_create_infos[0].generalShader = eRaygen;
    shader_group_create_infos[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[0].intersectionShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[0].pShaderGroupCaptureReplayHandle = nullptr;

    shader_groups.push_back(shader_group_create_infos[0]);

    shader_group_create_infos[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shader_group_create_infos[1].pNext = nullptr;
    shader_group_create_infos[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shader_group_create_infos[1].generalShader = eMiss;
    shader_group_create_infos[1].closestHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[1].intersectionShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[1].pShaderGroupCaptureReplayHandle = nullptr;

    shader_groups.push_back(shader_group_create_infos[1]);

    shader_group_create_infos[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shader_group_create_infos[2].pNext = nullptr;
    shader_group_create_infos[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shader_group_create_infos[2].generalShader = eMiss2;
    shader_group_create_infos[2].closestHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[2].intersectionShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[2].pShaderGroupCaptureReplayHandle = nullptr;

    shader_groups.push_back(shader_group_create_infos[2]);

    shader_group_create_infos[3].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shader_group_create_infos[3].pNext = nullptr;
    shader_group_create_infos[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    shader_group_create_infos[3].generalShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[3].closestHitShader = eClosestHit;
    shader_group_create_infos[3].anyHitShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[3].intersectionShader = VK_SHADER_UNUSED_KHR;
    shader_group_create_infos[3].pShaderGroupCaptureReplayHandle = nullptr;

    shader_groups.push_back(shader_group_create_infos[3]);

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipeline_layout_create_info.pSetLayouts = descriptorSetLayouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &pc_ranges;

    VkResult result =
      vkCreatePipelineLayout(device->getLogicalDevice(), &pipeline_layout_create_info, nullptr, &pipeline_layout);
    ASSERT_VULKAN(result, "Failed to create raytracing pipeline layout!")

    VkPipelineLibraryCreateInfoKHR pipeline_library_create_info{};
    pipeline_library_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
    pipeline_library_create_info.pNext = nullptr;
    pipeline_library_create_info.libraryCount = 0;
    pipeline_library_create_info.pLibraries = nullptr;

    VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_create_info{};
    raytracing_pipeline_create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    raytracing_pipeline_create_info.pNext = nullptr;
    raytracing_pipeline_create_info.flags = 0;
    raytracing_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    raytracing_pipeline_create_info.pStages = shader_stages.data();
    raytracing_pipeline_create_info.groupCount = static_cast<uint32_t>(shader_groups.size());
    raytracing_pipeline_create_info.pGroups = shader_groups.data();
    /*raytracing_pipeline_create_info.pLibraryInfo =
       &pipeline_library_create_info;
          raytracing_pipeline_create_info.pLibraryInterface = NULL;*/
    // TODO: HARDCODED FOR NOW;
    raytracing_pipeline_create_info.maxPipelineRayRecursionDepth = 2;
    raytracing_pipeline_create_info.layout = pipeline_layout;

    result = pvkCreateRayTracingPipelinesKHR(device->getLogicalDevice(),
      VK_NULL_HANDLE,
      VK_NULL_HANDLE,
      1,
      &raytracing_pipeline_create_info,
      nullptr,
      &graphicsPipeline);

    ASSERT_VULKAN(result, "Failed to create raytracing pipeline!")

    vkDestroyShaderModule(device->getLogicalDevice(), raygen_shader_module, nullptr);
    vkDestroyShaderModule(device->getLogicalDevice(), raymiss_shader_module, nullptr);
    vkDestroyShaderModule(device->getLogicalDevice(), raychit_shader_module, nullptr);
    vkDestroyShaderModule(device->getLogicalDevice(), shadow_shader_module, nullptr);
}

void KataglyphisRenderer::VulkanRendererInternals::Raytracing::createSBT()
{
    // load in functionality for raytracing shader group handles
    PFN_vkGetRayTracingShaderGroupHandlesKHR pvkGetRayTracingShaderGroupHandlesKHR =
      (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(
        device->getLogicalDevice(), "vkGetRayTracingShaderGroupHandlesKHR");

    raytracing_properties = VkPhysicalDeviceRayTracingPipelinePropertiesKHR{};
    raytracing_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &raytracing_properties;

    vkGetPhysicalDeviceProperties2(device->getPhysicalDevice(), &properties);

    uint32_t handle_size = raytracing_properties.shaderGroupHandleSize;
    uint32_t handle_size_aligned = align_up(handle_size, raytracing_properties.shaderGroupHandleAlignment);

    uint32_t group_count = static_cast<uint32_t>(shader_groups.size());
    uint32_t sbt_size = group_count * handle_size_aligned;

    std::vector<uint8_t> handles(sbt_size);

    VkResult result = pvkGetRayTracingShaderGroupHandlesKHR(
      device->getLogicalDevice(), graphicsPipeline, 0, group_count, sbt_size, handles.data());
    ASSERT_VULKAN(result, "Failed to get ray tracing shader group handles!")

    const VkBufferUsageFlags bufferUsageFlags =
      VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VkMemoryPropertyFlags memoryUsageFlags =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    raygenShaderBindingTableBuffer.create(device, handle_size, bufferUsageFlags, memoryUsageFlags);

    missShaderBindingTableBuffer.create(device, 2 * handle_size, bufferUsageFlags, memoryUsageFlags);

    hitShaderBindingTableBuffer.create(device, handle_size, bufferUsageFlags, memoryUsageFlags);

    void *mapped_raygen = nullptr;
    vkMapMemory(device->getLogicalDevice(),
      raygenShaderBindingTableBuffer.getBufferMemory(),
      0,
      VK_WHOLE_SIZE,
      0,
      &mapped_raygen);

    void *mapped_miss = nullptr;
    vkMapMemory(
      device->getLogicalDevice(), missShaderBindingTableBuffer.getBufferMemory(), 0, VK_WHOLE_SIZE, 0, &mapped_miss);

    void *mapped_rchit = nullptr;
    vkMapMemory(
      device->getLogicalDevice(), hitShaderBindingTableBuffer.getBufferMemory(), 0, VK_WHOLE_SIZE, 0, &mapped_rchit);

    memcpy(mapped_raygen, handles.data(), handle_size);
    memcpy(mapped_miss, handles.data() + handle_size_aligned, handle_size * 2);
    memcpy(mapped_rchit, handles.data() + handle_size_aligned * 3, handle_size);
}
