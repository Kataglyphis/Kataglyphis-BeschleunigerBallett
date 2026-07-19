module;
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

export module kataglyphis.vulkan.renderer;

import kataglyphis.vulkan.as_manager;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.descriptor_set_group;
import kataglyphis.vulkan.device;
import kataglyphis.vulkan.global_ubo;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.gui_renderer_shared_vars;
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
    bool supportsHardwareRaytracing() const;

    void cleanUp();

    void recreateSwapChain();

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
    Camera *camera;

    // -- pools
    bool record_commands(uint32_t image_index);
    void create_command_pool();
    void cleanUpCommandPools();
    vk::CommandPool graphics_command_pool{};
    vk::CommandPool compute_command_pool{};

    // uniform buffers
    VulkanRendererInternals::GlobalUBO globalUBO{};
    std::vector<VulkanBuffer> globalUBOBuffer;
    std::vector<void*> globalUBOMapped;
    VulkanRendererInternals::SceneUBO sceneUBO{};
    std::vector<VulkanBuffer> sceneUBOBuffer;
    std::vector<void*> sceneUBOMapped;
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

    // -- synchronization
    uint32_t current_frame{ 0 };
    uint32_t frame_sync_count{ 1 };
    std::vector<vk::Semaphore> image_available;
    std::vector<vk::Semaphore> render_finished_by_image;
    std::vector<vk::Fence> in_flight_fences;
    std::vector<vk::Fence> images_in_flight_fences;
    void createSynchronization();
    void cleanUpSync();

    // -- per-pass GPU timing (timestamp query pool)
    // Layout: one slice of GPU_TIMING_QUERIES_PER_IMAGE queries per swapchain
    // image, two queries (start/end) per timed pass. A frame resets only its
    // own image's slice inside its command buffer, and results are read back
    // for that slice on the NEXT use of the image (after its fence signaled),
    // so queries are never reset while still in flight.
    static constexpr uint32_t GPU_TIMING_QUERIES_PER_PASS = 2;
    static constexpr uint32_t GPU_TIMING_QUERIES_PER_IMAGE =
      GPU_TIMING_QUERIES_PER_PASS
      * static_cast<uint32_t>(VulkanRendererInternals::FrontendShared::GPU_TIMED_PASS_COUNT);
    bool gpu_timings_supported{ false };
    float gpu_timestamp_period{ 0.0f };
    uint64_t gpu_timestamp_mask{ ~0ULL };
    vk::QueryPool gpu_timing_query_pool{};
    // Per swapchain image: bitmask of passes actually recorded last time.
    std::vector<uint32_t> gpu_timing_pass_mask;
    // Per swapchain image: whether the slice was ever reset+written (freshly
    // created pools contain queries in an undefined state that must not be
    // read).
    std::vector<bool> gpu_timing_slice_recorded;

    struct GpuPassAverage
    {
        static constexpr uint32_t WINDOW = 30;
        std::array<float, WINDOW> samples{};
        uint32_t count{ 0 };
        uint32_t next{ 0 };
        float sum{ 0.0f };
        float add(float value)
        {
            if (count == WINDOW) {
                sum -= samples[next];
            } else {
                count++;
            }
            samples[next] = value;
            sum += value;
            next = (next + 1) % WINDOW;
            return sum / static_cast<float>(count);
        }
        void reset()
        {
            count = 0;
            next = 0;
            sum = 0.0f;
        }
    };
    std::array<GpuPassAverage, VulkanRendererInternals::FrontendShared::GPU_TIMED_PASS_COUNT> gpu_pass_averages{};

    void createGpuTimingResources();
    void destroyGpuTimingResources();
    // Reads back the previous results of image_index's slice (never waits) and
    // publishes smoothed per-pass milliseconds to the GUI shared vars.
    void readGpuTimings(uint32_t image_index);

    Kataglyphis::VulkanRendererInternals::ASManager asManager;
    VulkanBuffer objectDescriptionBuffer;
    void create_object_description_buffer();
    void updateObjectDescriptionDescriptorSets();

    // -- descriptor set groups (each owns one layout + pool + per-swapchain-
    // image sets; see kataglyphis.vulkan.descriptor_set_group)
    DescriptorSetGroup sharedRenderDescriptors;
    void createSharedRenderDescriptorResources();
    void updateTexturesInSharedRenderDescriptorSet();
    void updateUBODescriptorSets();

    DescriptorSetGroup postDescriptors;
    void create_post_descriptor_resources();
    void updatePostDescriptorSets();

    DescriptorSetGroup gbufferDescriptors;
    void create_gbuffer_descriptor_resources();
    void updateGBufferDescriptorSets();

    DescriptorSetGroup raytracingDescriptors;
    void createRaytracingDescriptorResources();
    void updateRaytracingDescriptorSets();

    void updateAllDescriptorSets();
    void cleanUpDescriptorResources();
    void initDescriptorResources();

    bool checkChangedFramebufferSize();
    bool device_lost_detected{ false };
};
}// namespace Kataglyphis
