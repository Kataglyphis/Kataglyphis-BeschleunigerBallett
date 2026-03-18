module;
#include <vector>
#include <vulkan/vulkan.hpp>

#include "renderer/accelerationStructures/BottomLevelAccelerationStructure.hpp"
#include "renderer/accelerationStructures/TopLevelAccelerationStructure.hpp"

export module kataglyphis.vulkan.as_manager;

import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.mesh;
import kataglyphis.vulkan.buffer;

export namespace Kataglyphis::VulkanRendererInternals {
struct BuildAccelerationStructure
{
    vk::AccelerationStructureBuildGeometryInfoKHR build_info;
    vk::AccelerationStructureBuildSizesInfoKHR size_info;
    const vk::AccelerationStructureBuildRangeInfoKHR *range_info;
    BottomLevelAccelerationStructure single_blas;
};

struct BlasInput
{
    std::vector<vk::AccelerationStructureGeometryKHR> as_geometry;
    std::vector<vk::AccelerationStructureBuildRangeInfoKHR> as_build_offset_info;
};

class ASManager
{
  public:
    ASManager();

    vk::AccelerationStructureKHR &getTLAS() { return tlas.vulkanAS; };

    void createASForScene(VulkanDevice *device, vk::CommandPool commandPool, Kataglyphis::Scene *scene);

    void createBLAS(VulkanDevice *device, vk::CommandPool commandPool, Kataglyphis::Scene *scene);

    void createTLAS(VulkanDevice *device, vk::CommandPool commandPool, Kataglyphis::Scene *scene);

    void cleanUp();

    ~ASManager();

  private:
    VulkanDevice *vulkanDevice{ nullptr };
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;
    Kataglyphis::VulkanBufferManager vulkanBufferManager;

    std::vector<BottomLevelAccelerationStructure> blas;
    TopLevelAccelerationStructure tlas;

    static void createSingleBlas(VulkanDevice *device,
      vk::CommandBuffer command_buffer,
      BuildAccelerationStructure &build_as_structure,
      vk::DeviceAddress scratch_device_or_host_address);

    static void createAccelerationStructureInfosBLAS(VulkanDevice *device,
      BuildAccelerationStructure &build_as_structure,
      BlasInput &blas_input,
      vk::DeviceSize &current_scretch_size,
      vk::DeviceSize &current_size);

    static void objectToVkGeometryKHR(VulkanDevice *device,
      Kataglyphis::Mesh *mesh,
      vk::AccelerationStructureGeometryKHR &acceleration_structure_geometry,
      vk::AccelerationStructureBuildRangeInfoKHR &acceleration_structure_build_range_info);
};
}// namespace Kataglyphis::VulkanRendererInternals
