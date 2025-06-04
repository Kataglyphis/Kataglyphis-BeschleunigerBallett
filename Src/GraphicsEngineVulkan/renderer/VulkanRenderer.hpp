#pragma once

#include "GlobalUBO.hpp"
#include "PathTracing.hpp"
#include "PostStage.hpp"
#include "gui/GUI.hpp"
#include "memory/Allocator.hpp"
#include "renderer/CommandBufferManager.hpp"
#include "renderer/accelerationStructures/ASManager.hpp"

#include "Rasterizer.hpp"
#include "Raytracing.hpp"
#include "SceneUBO.hpp"
#include "scene/Scene.hpp"
#include "scene/Texture.hpp"

#include "scene/Camera.hpp"
#include "vulkan_base/VulkanBuffer.hpp"
#include "vulkan_base/VulkanBufferManager.hpp"
#include "vulkan_base/VulkanDevice.hpp"
#include "vulkan_base/VulkanInstance.hpp"
#include "vulkan_base/VulkanSwapChain.hpp"
#include "window/Window.hpp"

namespace KataglyphisRenderer {
class VulkanRenderer
{
  public:
    VulkanRenderer(KataglyphisRenderer::Frontend::Window *window,
      Scene *scene,
      KataglyphisRenderer::Frontend::GUI *gui,
      Camera *camera);

    void drawFrame();

    void updateUniforms(Scene *scene, Camera *camera, KataglyphisRenderer::Frontend::Window *window);

    void updateStateDueToUserInput(KataglyphisRenderer::Frontend::GUI *gui);
    void finishAllRenderCommands();
    void update_raytracing_descriptor_set(uint32_t image_index);

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
    VkSurfaceKHR surface;
    void create_surface();

    std::unique_ptr<VulkanDevice> device;

    VulkanSwapChain vulkanSwapChain;

    KataglyphisRenderer::Frontend::Window *window;
    Scene *scene;
    KataglyphisRenderer::Frontend::GUI *gui;

    // -- pools
    void record_commands(uint32_t image_index);
    void create_command_pool();
    void cleanUpCommandPools();
    VkCommandPool graphics_command_pool;
    VkCommandPool compute_command_pool;

    // uniform buffers
    VulkanRendererInternals::GlobalUBO globalUBO;
    std::vector<VulkanBuffer> globalUBOBuffer;
    VulkanRendererInternals::SceneUBO sceneUBO;
    std::vector<VulkanBuffer> sceneUBOBuffer;
    void create_uniform_buffers();
    void update_uniform_buffers(uint32_t image_index);
    void cleanUpUBOs();

    std::vector<VkCommandBuffer> command_buffers;
    KataglyphisRenderer::VulkanRendererInternals::CommandBufferManager commandBufferManager;
    void create_command_buffers();

    KataglyphisRenderer::VulkanRendererInternals::Raytracing raytracingStage;
    KataglyphisRenderer::VulkanRendererInternals::Rasterizer rasterizer;
    PathTracing pathTracing;
    PostStage postStage;

    // new era of memory management for my project
    // for now on integrate vma
    Allocator allocator;

    // -- synchronization
    uint32_t current_frame{ 0 };
    std::vector<VkSemaphore> image_available;
    std::vector<VkSemaphore> render_finished;
    std::vector<VkFence> in_flight_fences;
    std::vector<VkFence> images_in_flight_fences;
    void createSynchronization();
    void cleanUpSync();

    ASManager asManager;
    VulkanBuffer objectDescriptionBuffer;
    void create_object_description_buffer();

    VkDescriptorPool descriptorPoolSharedRenderStages;
    void createDescriptorPoolSharedRenderStages();
    VkDescriptorSetLayout sharedRenderDescriptorSetLayout;
    void createSharedRenderDescriptorSetLayouts();
    std::vector<VkDescriptorSet> sharedRenderDescriptorSet;
    void createSharedRenderDescriptorSet();
    void updateTexturesInSharedRenderDescriptorSet();

    VkDescriptorPool post_descriptor_pool{ VK_NULL_HANDLE };
    VkDescriptorSetLayout post_descriptor_set_layout{ VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> post_descriptor_set;
    void create_post_descriptor_layout();
    void updatePostDescriptorSets();

    VkDescriptorPool raytracingDescriptorPool{ VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> raytracingDescriptorSet;
    VkDescriptorSetLayout raytracingDescriptorSetLayout{ VK_NULL_HANDLE };

    void createRaytracingDescriptorSetLayouts();
    void createRaytracingDescriptorSets();
    void updateRaytracingDescriptorSets();
    void createRaytracingDescriptorPool();

    bool checkChangedFramebufferSize();
};
}// namespace KataglyphisRenderer