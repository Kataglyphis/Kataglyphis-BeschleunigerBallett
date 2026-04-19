module;
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.renderer;

import kataglyphis.vulkan.allocator;
import kataglyphis.vulkan.as_manager;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.global_ubo;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.instance;
import kataglyphis.vulkan.path_tracing;
import kataglyphis.vulkan.post_stage;
import kataglyphis.vulkan.rasterizer;
import kataglyphis.vulkan.deferred_rasterizer;
import kataglyphis.vulkan.raytracing;
import kataglyphis.vulkan.cascaded_shadow_map;
import kataglyphis.vulkan.omni_dir_shadow_map;
import kataglyphis.vulkan.sky_box;
import kataglyphis.vulkan.clouds;
import kataglyphis.vulkan.scene_ubo;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.window;

export namespace Kataglyphis {
class VulkanRenderer
{
  public:
    VulkanRenderer(Kataglyphis::Frontend::Window *window,
      Scene *scene,
      Kataglyphis::Frontend::GUI *gui,
      Camera *camera);

    void drawFrame();

    void updateUniforms(Scene *scene_data, Camera *camera_data, Kataglyphis::Frontend::Window *window_data);

    void updateStateDueToUserInput(Kataglyphis::Frontend::GUI *frontend_gui);
    void finishAllRenderCommands();
    void update_raytracing_descriptor_set(uint32_t image_index);
    bool hasDeviceLost() const { return device_lost_detected; }

    void cleanUp();

    ~VulkanRenderer();

  private:
    void shaderHotReload();

    // helper class for managing our buffers
    VulkanBufferManager vulkanBufferManager;

    // Vulkan instance, stores all per-application states
    VulkanInstance instance;

    // surface defined on windows as WIN32 window system, Linux f.e. X11, MacOS
    // also their own
    vk::SurfaceKHR surface{};
    void create_surface();

    std::shared_ptr<VulkanDevice> device;

    VulkanSwapChain vulkanSwapChain;

    Kataglyphis::Frontend::Window *window;
    Scene *scene;
    Kataglyphis::Frontend::GUI *gui;

    // -- pools
    bool record_commands(uint32_t image_index);
    void create_command_pool();
    void cleanUpCommandPools();
    vk::CommandPool graphics_command_pool{};
    vk::CommandPool compute_command_pool{};

    // uniform buffers
    VulkanRendererInternals::GlobalUBO globalUBO{};
    std::vector<VulkanBuffer> globalUBOBuffer;
    VulkanRendererInternals::SceneUBO sceneUBO{};
    std::vector<VulkanBuffer> sceneUBOBuffer;
    void create_uniform_buffers();
    void update_uniform_buffers(uint32_t image_index);
    void cleanUpUBOs();

    std::vector<vk::CommandBuffer> command_buffers;
    Kataglyphis::VulkanRendererInternals::CommandBufferManager commandBufferManager;
    void create_command_buffers();

    Kataglyphis::VulkanRendererInternals::Raytracing raytracingStage;
    Kataglyphis::VulkanRendererInternals::Rasterizer rasterizer;
    Kataglyphis::VulkanRendererInternals::DeferredRasterizer deferredRasterizer;
    Kataglyphis::VulkanRendererInternals::PathTracing pathTracing;
    Kataglyphis::VulkanRendererInternals::PostStage postStage;

    // Atmospheric & Lighting Additions
    Kataglyphis::SkyBox skyBox;
    Kataglyphis::Clouds clouds;
    Kataglyphis::CascadedShadowMap dirShadowMap;
    Kataglyphis::OmniDirShadowMap pointShadowMap;

    // new era of memory management for my project
    // for now on integrate vma
    Allocator allocator;

    // -- synchronization
    uint32_t current_frame{ 0 };
    uint32_t frame_sync_count{ 1 };
    std::vector<vk::Semaphore> image_available;
    std::vector<vk::Semaphore> render_finished_by_image;
    std::vector<vk::Fence> in_flight_fences;
    std::vector<vk::Fence> images_in_flight_fences;
    void createSynchronization();
    void cleanUpSync();

    Kataglyphis::VulkanRendererInternals::ASManager asManager;
    VulkanBuffer objectDescriptionBuffer;
    void create_object_description_buffer();

    vk::DescriptorPool descriptorPoolSharedRenderStages{};
    void createDescriptorPoolSharedRenderStages();
    vk::DescriptorSetLayout sharedRenderDescriptorSetLayout{};
    void createSharedRenderDescriptorSetLayouts();
    std::vector<vk::DescriptorSet> sharedRenderDescriptorSet;
    void createSharedRenderDescriptorSet();
    void updateTexturesInSharedRenderDescriptorSet();

    vk::DescriptorPool post_descriptor_pool{};
    vk::DescriptorSetLayout post_descriptor_set_layout{};
    std::vector<vk::DescriptorSet> post_descriptor_set;
    void create_post_descriptor_layout();
    void updatePostDescriptorSets();

    vk::DescriptorPool gbuffer_descriptor_pool{};
    vk::DescriptorSetLayout gbuffer_descriptor_set_layout{};
    std::vector<vk::DescriptorSet> gbuffer_descriptor_set;
    void create_gbuffer_descriptor_layout();
    void updateGBufferDescriptorSets();

    vk::DescriptorPool raytracingDescriptorPool{};
    std::vector<vk::DescriptorSet> raytracingDescriptorSet;
    vk::DescriptorSetLayout raytracingDescriptorSetLayout{};

    void createRaytracingDescriptorSetLayouts();
    void createRaytracingDescriptorSets();
    void updateRaytracingDescriptorSets();
    void createRaytracingDescriptorPool();

    bool checkChangedFramebufferSize();
    bool device_lost_detected{ false };
};
}// namespace Kataglyphis
