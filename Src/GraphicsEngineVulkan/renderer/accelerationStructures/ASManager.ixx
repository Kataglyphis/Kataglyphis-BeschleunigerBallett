module;
#include <vector>
#include <vulkan/vulkan.h>

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
    VkAccelerationStructureBuildGeometryInfoKHR build_info;
    VkAccelerationStructureBuildSizesInfoKHR size_info;
    const VkAccelerationStructureBuildRangeInfoKHR *range_info;
    BottomLevelAccelerationStructure single_blas;
};

struct BlasInput
{
    std::vector<VkAccelerationStructureGeometryKHR> as_geometry;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> as_build_offset_info;
};

class ASManager
{
  public:
    ASManager();

    VkAccelerationStructureKHR &getTLAS() { return tlas.vulkanAS; };

    void createASForScene(VulkanDevice *device, VkCommandPool commandPool, Kataglyphis::Scene *scene);

    void createBLAS(VulkanDevice *device, VkCommandPool commandPool, Kataglyphis::Scene *scene);

    void createTLAS(VulkanDevice *device, VkCommandPool commandPool, Kataglyphis::Scene *scene);

    void cleanUp();

    ~ASManager();

  private:
    VulkanDevice *vulkanDevice{ VK_NULL_HANDLE };
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;
    Kataglyphis::VulkanBufferManager vulkanBufferManager;

    std::vector<BottomLevelAccelerationStructure> blas;
    TopLevelAccelerationStructure tlas;

    static void createSingleBlas(VulkanDevice *device,
      VkCommandBuffer command_buffer,
      BuildAccelerationStructure &build_as_structure,
      VkDeviceAddress scratch_device_or_host_address);

    static void createAccelerationStructureInfosBLAS(VulkanDevice *device,
      BuildAccelerationStructure &build_as_structure,
      BlasInput &blas_input,
      VkDeviceSize &current_scretch_size,
      VkDeviceSize &current_size);

    static void objectToVkGeometryKHR(VulkanDevice *device,
      Kataglyphis::Mesh *mesh,
      VkAccelerationStructureGeometryKHR &acceleration_structure_geometry,
      VkAccelerationStructureBuildRangeInfoKHR &acceleration_structure_build_range_info);
};
}// namespace Kataglyphis::VulkanRendererInternals
