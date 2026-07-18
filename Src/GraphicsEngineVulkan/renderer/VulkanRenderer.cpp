module;
#include "common/Utilities.hpp"
#include "hostDevice/host_device_shared_vars.hpp"
#include "renderer/pushConstants/PushConstantPost.hpp"
#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include "renderer/pushConstants/PushConstantRayTracing.hpp"
#include "spdlog/spdlog.h"

#include <cstdint>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <tuple>
#include <vector>

#ifndef VMA_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#endif// !VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <imgui.h>
#include <imgui_internal.h>

#include "common/Globals.hpp"
#include "renderer/SceneUBO.hpp"

module kataglyphis.vulkan.renderer;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.gui_renderer_shared_vars;
import kataglyphis.vulkan.gui_scene_shared_vars;
import kataglyphis.vulkan.object_description;
import kataglyphis.vulkan.queue_family_indices;
import kataglyphis.vulkan.debug;
import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.scene_config;
import kataglyphis.vulkan.texture;
import kataglyphis.vulkan.as_manager;
import kataglyphis.vulkan.buffer_manager;
import kataglyphis.vulkan.buffer;
import kataglyphis.vulkan.camera;
import kataglyphis.vulkan.command_buffer_manager;
import kataglyphis.vulkan.instance;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.scene_ubo;
import kataglyphis.vulkan.global_ubo;
import kataglyphis.vulkan.swapchain;
import kataglyphis.vulkan.allocator;
import kataglyphis.vulkan.window;

namespace {
[[maybe_unused]] vk::Result toVkResult(VkResult result) { return static_cast<vk::Result>(result); }
}// namespace

Kataglyphis::VulkanRenderer::VulkanRenderer(Kataglyphis::Frontend::Window *window,
  Scene *scene,
  Kataglyphis::Frontend::GUI *gui,
  Camera *camera)
  : window(window), scene(scene), gui(gui), camera(camera)
{
    instance = VulkanInstance();

    vk::DebugReportFlagsEXT const debugReportFlags =
      vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning;
    if (Kataglyphis::ENABLE_VALIDATION_LAYERS) {
        debug::setupDebugging(instance.getVulkanInstance(), debugReportFlags, nullptr);
    }

    create_surface();

    device = std::make_shared<VulkanDevice>(&instance, &surface);

    allocator = Allocator(device->getLogicalDevice(), device->getPhysicalDevice(), instance.getVulkanInstance());

    create_command_pool();

    vulkanSwapChain.initVulkanContext(device, window, surface);
    create_uniform_buffers();
    create_command_buffers();

    createSynchronization();

    initDescriptorResources();

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_rasterizer = { sharedRenderDescriptorSetLayout };
    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_deferred = { sharedRenderDescriptorSetLayout, gbuffer_descriptor_set_layout };
    
    rasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_rasterizer, graphics_command_pool);
    deferredRasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_deferred, graphics_command_pool);

    clouds.init(device, graphics_command_pool, sharedRenderDescriptorSetLayout, vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);
    dirShadowMap.init(device, 2048, 2048, MAX_CASCADES);
    dirShadowMap.createGraphicsPipeline();
    pointShadowMap.init(device, 1024, 1024);

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_post = { post_descriptor_set_layout };
    postStage.init(device, &vulkanSwapChain, descriptor_set_layouts_post);

    if (device->supportsHardwareAcceleratedRRT()) {
        createRaytracingDescriptorPool();
        createRaytracingDescriptorSetLayouts();
        createRaytracingDescriptorSets();

        std::vector<vk::DescriptorSetLayout> const layouts = { sharedRenderDescriptorSetLayout,
            raytracingDescriptorSetLayout };
        raytracingStage.init(device, layouts, &vulkanSwapChain);
        pathTracing.init(device, layouts);
    }

    updateUniforms(scene, camera, window);
    updateAllDescriptorSets();

    std::vector<vk::ImageView> skyboxImageViews(vulkanSwapChain.getNumberSwapChainImages());
    std::vector<vk::ImageView> skyboxDepthViews(vulkanSwapChain.getNumberSwapChainImages());
    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        skyboxImageViews[i] = vulkanSwapChain.getSwapChainImage(i).getImageView();
        skyboxDepthViews[i] = postStage.getDepthBufferImageView();
    }

    skyBox.init(device, graphics_command_pool);
    skyBox.createRenderPass(vulkanSwapChain.getSwapChainFormat(), postStage.getDepthFormat());
    skyBox.createGraphicsPipeline(sharedRenderDescriptorSetLayout);
    skyBox.createFramebuffers(vulkanSwapChain.getNumberSwapChainImages(), skyboxImageViews, skyboxDepthViews,
        vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);

    scene->loadModel(device, graphics_command_pool);
    
    if (device->supportsHardwareAcceleratedRRT()) {
        asManager.createASForScene(device, graphics_command_pool, scene);
    }

    create_object_description_buffer();
    
    // Final update after model loading
    updateAllDescriptorSets();

    gui->initializeVulkanContext(device,
      instance.getVulkanInstance(),
      postStage.getRenderPass(),
      graphics_command_pool,
      vulkanSwapChain.getNumberSwapChainImages());
    gui->setUserSelectionForRRT(device->supportsHardwareAcceleratedRRT());
}

void Kataglyphis::VulkanRenderer::updateUniforms(Scene *scene_data,
  Camera *camera_data,
  [[maybe_unused]] Kataglyphis::Frontend::Window *window_data)
{
    const GUISceneSharedVars guiSceneSharedVars = scene_data->getGuiSceneSharedVars();

    const vk::Extent2D extent = vulkanSwapChain.getSwapChainExtent();
    float const aspect_ratio = (extent.height > 0) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

    globalUBO.view = camera_data->calculate_viewmatrix();
    globalUBO.projection = glm::perspective(glm::radians(camera_data->get_fov()),
      aspect_ratio,
      camera_data->get_near_plane(),
      camera_data->get_far_plane());
    globalUBO.projection[1][1] *= -1;

    sceneUBO.view_dir = glm::vec4(camera_data->get_camera_direction().x, camera_data->get_camera_direction().y, camera_data->get_camera_direction().z, 1.0F);

    sceneUBO.dirLight.direction = glm::vec4(guiSceneSharedVars.directional_light_direction[0],
      guiSceneSharedVars.directional_light_direction[1],
      guiSceneSharedVars.directional_light_direction[2],
      1.0F);

    sceneUBO.dirLight.color = glm::vec4(guiSceneSharedVars.directional_light_color[0],
      guiSceneSharedVars.directional_light_color[1],
      guiSceneSharedVars.directional_light_color[2],
      guiSceneSharedVars.direcional_light_radiance);

    sceneUBO.cam_pos = glm::vec4(camera_data->get_camera_position().x, camera_data->get_camera_position().y, camera_data->get_camera_position().z, camera_data->get_fov());

    // Populate GUI state into SceneUBO
    sceneUBO.pcfRadius = static_cast<unsigned int>(guiSceneSharedVars.pcf_radius);
    sceneUBO.cascadedShadowIntensity = guiSceneSharedVars.cascaded_shadow_intensity;

    // Calculate CSM cascades
    dirShadowMap.updateCascades(globalUBO.view, camera_data->get_fov(),
        aspect_ratio,
        camera_data->get_near_plane(), camera_data->get_far_plane(),
        glm::vec3(sceneUBO.dirLight.direction));

    const auto& cascadeData = dirShadowMap.getCascadeData();
    for (size_t i = 0; i < std::min(cascadeData.size(), static_cast<size_t>(MAX_CASCADES)); ++i) {
        sceneUBO.cascadeSplits[static_cast<int>(i)] = cascadeData[i].splitDepth;
        sceneUBO.cascadeLightSpaceMatrices[i] = cascadeData[i].viewProjMatrix;
    }

    sceneUBO.cloudMovementDirection = glm::vec4(
        guiSceneSharedVars.cloud_movement_direction[0],
        guiSceneSharedVars.cloud_movement_direction[1],
        guiSceneSharedVars.cloud_movement_direction[2],
        static_cast<float>(guiSceneSharedVars.cloud_speed));

    sceneUBO.cloudMeshScale = glm::vec4(
        guiSceneSharedVars.cloud_mesh_scale[0],
        guiSceneSharedVars.cloud_mesh_scale[1],
        guiSceneSharedVars.cloud_mesh_scale[2],
        guiSceneSharedVars.cloud_scale);

    sceneUBO.cloudMeshOffset = glm::vec4(
        guiSceneSharedVars.cloud_mesh_offset[0],
        guiSceneSharedVars.cloud_mesh_offset[1],
        guiSceneSharedVars.cloud_mesh_offset[2],
        guiSceneSharedVars.cloud_density);

    sceneUBO.cloudParameters = glm::vec4(
        guiSceneSharedVars.cloud_pillowness,
        guiSceneSharedVars.cloud_cirrus_effect,
        guiSceneSharedVars.cloud_powder_effect ? 1.0f : 0.0f,
        static_cast<float>(guiSceneSharedVars.cloud_num_march_steps));
}

auto Kataglyphis::VulkanRenderer::supportsHardwareRaytracing() const -> bool
{
    return device && device->supportsHardwareAcceleratedRRT();
}

void Kataglyphis::VulkanRenderer::updateStateDueToUserInput(Kataglyphis::Frontend::GUI *frontend_gui)
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &guiRendererSharedVars =
      frontend_gui->getGuiRendererSharedVars();

    if (guiRendererSharedVars.shader_hot_reload_triggered) {
        shaderHotReload();
        guiRendererSharedVars.shader_hot_reload_triggered = false;
    }

    GUISceneSharedVars &guiSceneSharedVars = scene->getGuiSceneSharedVars();
    if (guiSceneSharedVars.shadow_resolution_changed) {
        guiSceneSharedVars.shadow_resolution_changed = false;

        (void)device->getLogicalDevice().waitIdle();
        dirShadowMap.cleanUp();
        
        uint32_t shadow_res = 512;
        if (guiSceneSharedVars.shadow_map_res_index == 1) shadow_res = 1024;
        else if (guiSceneSharedVars.shadow_map_res_index == 2) shadow_res = 2048;
        else if (guiSceneSharedVars.shadow_map_res_index == 3) shadow_res = 4096;

        dirShadowMap.init(device, shadow_res, shadow_res, static_cast<uint32_t>(guiSceneSharedVars.num_shadow_cascades));
        
        // We must recreate descriptor sets that depend on the shadow map
        updateTexturesInSharedRenderDescriptorSet();
    }

    if (guiSceneSharedVars.model_transform_changed) {
        guiSceneSharedVars.model_transform_changed = false;
        frontend_gui->getGuiSceneSharedVars().model_transform_changed = false;

        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::scale(modelMatrix, glm::vec3(60.0f, 60.0f, 60.0f)); // Apply original scale
        
        // Apply world position directly to the matrix's translation column
        modelMatrix[3] = glm::vec4(guiSceneSharedVars.model_position[0], 
                                   guiSceneSharedVars.model_position[1], 
                                   guiSceneSharedVars.model_position[2], 
                                   1.0f);
        
        // ZYX rotation order
        modelMatrix = glm::rotate(modelMatrix, glm::radians(guiSceneSharedVars.model_rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(guiSceneSharedVars.model_rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(guiSceneSharedVars.model_rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
        
        if (guiSceneSharedVars.selected_model_index >= 0) {
            scene->update_model_matrix(modelMatrix, 0);
            
            // Re-upload object descriptions as the transform changed
            (void)device->getLogicalDevice().waitIdle();
            objectDescriptionBuffer.cleanUp();
            create_object_description_buffer();
            updateAllDescriptorSets();
        }


    }

    if (guiSceneSharedVars.model_reload_requested) {
        guiSceneSharedVars.model_reload_requested = false;

        const auto model_paths = sceneConfig::getAvailableModelPaths();
        const int sel = guiSceneSharedVars.selected_model_index;
        if (sel >= 0 && sel < static_cast<int>(model_paths.size())) {
            const std::string selected_path = model_paths[static_cast<size_t>(sel)];
            const std::string resolved_path = sceneConfig::resolveModelPath(selected_path);

            (void)device->getLogicalDevice().waitIdle();

            scene->reloadModel(device, graphics_command_pool, resolved_path);

            if (device->supportsHardwareAcceleratedRRT()) {
                asManager.createASForScene(device, graphics_command_pool, scene);
            }

            objectDescriptionBuffer.cleanUp();
            create_object_description_buffer();

            updateTexturesInSharedRenderDescriptorSet();
        }
    }
}

void Kataglyphis::VulkanRenderer::finishAllRenderCommands() { std::ignore = device->getLogicalDevice().waitIdle(); }

void Kataglyphis::VulkanRenderer::shaderHotReload()
{
    std::ignore = device->getLogicalDevice().waitIdle();

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts = { sharedRenderDescriptorSetLayout };
    rasterizer.shaderHotReload(descriptor_set_layouts);

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_post = { post_descriptor_set_layout };
    postStage.shaderHotReload(descriptor_set_layouts_post);

    if (device->supportsHardwareAcceleratedRRT()) {
        std::vector<vk::DescriptorSetLayout> const layouts = { sharedRenderDescriptorSetLayout,
            raytracingDescriptorSetLayout };
        raytracingStage.shaderHotReload(layouts);
        pathTracing.shaderHotReload(layouts);
    }
}

void Kataglyphis::VulkanRenderer::drawFrame()
{
    const auto end_imgui_frame_if_needed = []() -> void {
        ImGuiContext const *imgui_context = ImGui::GetCurrentContext();
        if (imgui_context != nullptr && imgui_context->WithinFrameScope) { ImGui::EndFrame(); }
    };

    const auto abort_frame_with_fatal_error = [&](const char *message, vk::Result error_code) -> void {
        spdlog::error(fmt::format("{} (vk::Result={})", message, static_cast<int>(error_code)));
        if (error_code == vk::Result::eErrorDeviceLost) { device_lost_detected = true; }
        if (window != nullptr && window->get_window() != nullptr) {
            glfwSetWindowShouldClose(window->get_window(), GLFW_TRUE);
        }
        end_imgui_frame_if_needed();
    };

    if (frame_sync_count == 0) {
        spdlog::error("No synchronization frames available; skipping draw frame.");
        end_imgui_frame_if_needed();
        return;
    }

    if (checkChangedFramebufferSize()) {
        if (frame_sync_count > 0 && !in_flight_fences.empty()) {
            recreateSwapChain();
        }
    }

    if (current_frame >= in_flight_fences.size() || current_frame >= image_available.size()) {
        spdlog::error(fmt::format("Frame synchronization index out of range: {}", current_frame));
        end_imgui_frame_if_needed();
        return;
    }

    if (!in_flight_fences[current_frame] || !image_available[current_frame]) {
        spdlog::error(fmt::format("Synchronization handles are invalid for frame {}.", current_frame));
        if (window != nullptr && window->get_window() != nullptr) {
            glfwSetWindowShouldClose(window->get_window(), GLFW_TRUE);
        }
        end_imgui_frame_if_needed();
        return;
    }

    vk::Result result = device->getLogicalDevice().waitForFences(
      1, &in_flight_fences[current_frame], VK_TRUE, std::numeric_limits<uint64_t>::max());
    if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to wait for fences!", result);
        return;
    }

    uint32_t image_index = 0;
    std::tie(result, image_index) = device->getLogicalDevice().acquireNextImageKHR(
      vulkanSwapChain.getSwapChain(), std::numeric_limits<uint64_t>::max(), image_available[current_frame], nullptr);

    if (result == vk::Result::eErrorOutOfDateKHR) {
        end_imgui_frame_if_needed();
        recreateSwapChain();
        return;
    }

    // eSuboptimalKHR still delivered a usable image and signaled the acquire
    // semaphore, so this frame must be rendered and presented; presentKHR's
    // result handling below recreates the swapchain afterwards.
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        abort_frame_with_fatal_error("Failed to acquire next image!", result);
        return;
    }

    if (image_index >= images_in_flight_fences.size() || image_index >= command_buffers.size()) {
        spdlog::error(fmt::format("Swapchain image index out of range: {}", image_index));
        end_imgui_frame_if_needed();
        return;
    }

    if (image_index >= render_finished_by_image.size() || !render_finished_by_image[image_index]) {
        spdlog::error(fmt::format("Render-finished semaphore missing for swapchain image {}.", image_index));
        end_imgui_frame_if_needed();
        return;
    }

    if (images_in_flight_fences[image_index]) {
        result =
          device->getLogicalDevice().waitForFences(1, &images_in_flight_fences[image_index], VK_TRUE, UINT64_MAX);
        if (result != vk::Result::eSuccess) {
            abort_frame_with_fatal_error("Failed to wait for image in-flight fence!", result);
            return;
        }
    }

    images_in_flight_fences[image_index] = in_flight_fences[current_frame];

    result = command_buffers[image_index].reset(vk::CommandBufferResetFlags{});
    if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to reset command buffer!", result);
        return;
    }

    vk::CommandBufferBeginInfo buffer_begin_info{};
    buffer_begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    result = command_buffers[image_index].begin(&buffer_begin_info);
    if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to start recording a command buffer!", result);
        return;
    }

    update_uniform_buffers(image_index);

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();
    const bool raytracing_available = device->supportsHardwareAcceleratedRRT();
    const char *const render_mode =
      (!raytracing_available || (!guiRendererSharedVars.raytracing && !guiRendererSharedVars.pathTracing))
        ? "rasterizer"
        : (guiRendererSharedVars.raytracing ? "raytracing" : "path_tracing");
    if (raytracing_available && guiRendererSharedVars.raytracing) { update_raytracing_descriptor_set(image_index); }

    if (!record_commands(image_index)) {
        end_imgui_frame_if_needed();
        return;
    }

    result = command_buffers[image_index].end();
    if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to stop recording a command buffer!", result);
        return;
    }

    vk::SubmitInfo submit_info{};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available[current_frame];

    vk::PipelineStageFlags const wait_stages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    submit_info.pWaitDstStageMask = &wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[image_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished_by_image[image_index];

    result = device->getLogicalDevice().resetFences(1, &in_flight_fences[current_frame]);
    if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to reset fences!", result);
        return;
    }

    result = device->getGraphicsQueue().submit(1, &submit_info, in_flight_fences[current_frame]);
    if (result != vk::Result::eSuccess) {
        spdlog::error(
          fmt::format("Queue submit context: frame={}, imageIndex={}, renderMode={}, supportsRRT={}, cmdBufferIndex={}",
            current_frame,
            image_index,
            render_mode,
            raytracing_available,
            image_index));
        abort_frame_with_fatal_error("Failed to submit command buffer to queue!", result);
        return;
    }

    vk::PresentInfoKHR present_info{};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_by_image[image_index];
    present_info.swapchainCount = 1;
    const vk::SwapchainKHR swapchain = vulkanSwapChain.getSwapChain();
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;

    result = device->getPresentationQueue().presentKHR(&present_info);

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        recreateSwapChain();
    } else if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to present image!", result);
        return;
    }

    current_frame = (current_frame + 1) % frame_sync_count;
}

bool Kataglyphis::VulkanRenderer::checkChangedFramebufferSize()
{
    if (window == nullptr) { return false; }

    if (window->framebuffer_size_has_changed()) {
        window->reset_framebuffer_has_changed();
        return true;
    }

    return false;
}

void Kataglyphis::VulkanRenderer::recreateSwapChain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window->get_window(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window->get_window(), &width, &height);
        glfwWaitEvents();
    }

    std::ignore = device->getLogicalDevice().waitIdle();

    uint32_t oldImageCount = vulkanSwapChain.getNumberSwapChainImages();

    // Destroy framebuffers that reference swapchain image views
    // before recreating the swapchain
    postStage.destroyFramebuffers();
    rasterizer.destroyFramebuffers();
    deferredRasterizer.destroyFramebuffers();
    skyBox.destroyFramebuffers();

    vulkanSwapChain.recreate(device, surface);

    uint32_t newImageCount = vulkanSwapChain.getNumberSwapChainImages();

    // Recreate depth buffers and framebuffers with new swapchain
    postStage.recreateFrameResources();
    rasterizer.recreateFrameResources(graphics_command_pool);
    deferredRasterizer.recreateFrameResources(graphics_command_pool);
    clouds.recreateFrameResources(graphics_command_pool, vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);

    std::vector<vk::ImageView> skyboxImageViews(vulkanSwapChain.getNumberSwapChainImages());
    std::vector<vk::ImageView> skyboxDepthViews(vulkanSwapChain.getNumberSwapChainImages());
    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        skyboxImageViews[i] = vulkanSwapChain.getSwapChainImage(i).getImageView();
        skyboxDepthViews[i] = postStage.getDepthBufferImageView();
    }

    skyBox.recreateFrameResources(vulkanSwapChain.getNumberSwapChainImages(), skyboxImageViews, skyboxDepthViews,
        vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);

    // If the image count changed, every per-swapchain-image resource must be
    // re-provisioned, not just the descriptor pools: the UBO vectors size the
    // shared descriptor pool and are indexed per image by the descriptor
    // updates below, and the raytracing pool/sets are allocated per image.
    if (newImageCount != oldImageCount) {
        cleanUpUBOs();
        create_uniform_buffers();

        cleanUpDescriptorResources();
        initDescriptorResources();

        if (device->supportsHardwareAcceleratedRRT()) {
            if (raytracingDescriptorPool) {
                device->getLogicalDevice().destroyDescriptorPool(raytracingDescriptorPool);
                raytracingDescriptorPool = nullptr;
            }
            raytracingDescriptorSet.clear();
            createRaytracingDescriptorPool();
            createRaytracingDescriptorSets();
        }

        // The freshly allocated shared sets lost the object-description
        // binding, which updateAllDescriptorSets() does not rewrite.
        updateObjectDescriptionDescriptorSets();
    }

    updateAllDescriptorSets();

    create_command_buffers();
    createSynchronization();
}

void Kataglyphis::VulkanRenderer::update_uniform_buffers(uint32_t image_index)
{
    if (image_index >= globalUBOMapped.size() || image_index >= sceneUBOMapped.size()) {
        spdlog::error(fmt::format("Uniform buffer index out of range: {}", image_index));
        return;
    }

    std::memcpy(globalUBOMapped[image_index], &globalUBO, sizeof(VulkanRendererInternals::GlobalUBO));
    std::memcpy(sceneUBOMapped[image_index], &sceneUBO, sizeof(VulkanRendererInternals::SceneUBO));
}

void Kataglyphis::VulkanRenderer::updateUBODescriptorSets()
{
    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vk::DescriptorBufferInfo globalUBO_buffer_info{};
        globalUBO_buffer_info.buffer = globalUBOBuffer[i].getBuffer();
        globalUBO_buffer_info.offset = 0;
        globalUBO_buffer_info.range = sizeof(globalUBO);

        vk::WriteDescriptorSet globalUBO_set_write{};
        globalUBO_set_write.dstSet = sharedRenderDescriptorSet[i];
        globalUBO_set_write.dstBinding = 0;
        globalUBO_set_write.dstArrayElement = 0;
        globalUBO_set_write.descriptorType = vk::DescriptorType::eUniformBuffer;
        globalUBO_set_write.descriptorCount = 1;
        globalUBO_set_write.pBufferInfo = &globalUBO_buffer_info;

        vk::DescriptorBufferInfo sceneUBO_buffer_info{};
        sceneUBO_buffer_info.buffer = sceneUBOBuffer[i].getBuffer();
        sceneUBO_buffer_info.offset = 0;
        sceneUBO_buffer_info.range = sizeof(sceneUBO);

        vk::WriteDescriptorSet sceneUBO_set_write{};
        sceneUBO_set_write.dstSet = sharedRenderDescriptorSet[i];
        sceneUBO_set_write.dstBinding = 1;
        sceneUBO_set_write.dstArrayElement = 0;
        sceneUBO_set_write.descriptorType = vk::DescriptorType::eUniformBuffer;
        sceneUBO_set_write.descriptorCount = 1;
        sceneUBO_set_write.pBufferInfo = &sceneUBO_buffer_info;

        std::vector<vk::WriteDescriptorSet> write_descriptor_sets = { globalUBO_set_write, sceneUBO_set_write };

        device->getLogicalDevice().updateDescriptorSets(
          static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
    }
}

void Kataglyphis::VulkanRenderer::updateAllDescriptorSets()
{
    updateUBODescriptorSets();
    updatePostDescriptorSets();
    updateGBufferDescriptorSets();
    updateTexturesInSharedRenderDescriptorSet();
    if (device->supportsHardwareAcceleratedRRT()) {
        updateRaytracingDescriptorSets();
    }
}

void Kataglyphis::VulkanRenderer::cleanUpDescriptorResources()
{
    if (descriptorPoolSharedRenderStages) {
        device->getLogicalDevice().destroyDescriptorPool(descriptorPoolSharedRenderStages);
        descriptorPoolSharedRenderStages = nullptr;
    }
    if (post_descriptor_pool) {
        device->getLogicalDevice().destroyDescriptorPool(post_descriptor_pool);
        post_descriptor_pool = nullptr;
    }
    if (gbuffer_descriptor_pool) {
        device->getLogicalDevice().destroyDescriptorPool(gbuffer_descriptor_pool);
        gbuffer_descriptor_pool = nullptr;
    }
    if (post_descriptor_set_layout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(post_descriptor_set_layout);
        post_descriptor_set_layout = nullptr;
    }
    if (gbuffer_descriptor_set_layout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(gbuffer_descriptor_set_layout);
        gbuffer_descriptor_set_layout = nullptr;
    }
    if (sharedRenderDescriptorSetLayout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(sharedRenderDescriptorSetLayout);
        sharedRenderDescriptorSetLayout = nullptr;
    }

    sharedRenderDescriptorSet.clear();
    post_descriptor_set.clear();
    gbuffer_descriptor_set.clear();
}

void Kataglyphis::VulkanRenderer::initDescriptorResources()
{
    createSharedRenderDescriptorSetLayouts();
    createDescriptorPoolSharedRenderStages();
    createSharedRenderDescriptorSet();
    create_post_descriptor_layout();
    create_gbuffer_descriptor_layout();
}

void Kataglyphis::VulkanRenderer::update_raytracing_descriptor_set(uint32_t image_index)
{
    if (image_index >= raytracingDescriptorSet.size()) {
        spdlog::error(fmt::format("Raytracing descriptor set index out of range: {}", image_index));
        return;
    }

    vk::WriteDescriptorSetAccelerationStructureKHR descriptor_set_acceleration_structure{};
    descriptor_set_acceleration_structure.accelerationStructureCount = 1;
    vk::AccelerationStructureKHR &vulkanTLAS = asManager.getTLAS();
    if (!vulkanTLAS) {
        return;
    }
    descriptor_set_acceleration_structure.pAccelerationStructures = &vulkanTLAS;

    vk::WriteDescriptorSet write_descriptor_set_acceleration_structure{};
    write_descriptor_set_acceleration_structure.pNext = &descriptor_set_acceleration_structure;
    write_descriptor_set_acceleration_structure.dstSet = raytracingDescriptorSet[image_index];
    write_descriptor_set_acceleration_structure.dstBinding = TLAS_BINDING;
    write_descriptor_set_acceleration_structure.dstArrayElement = 0;
    write_descriptor_set_acceleration_structure.descriptorCount = 1;
    write_descriptor_set_acceleration_structure.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;

    vk::DescriptorImageInfo image_info{};
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();
    Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(image_index) : deferredRasterizer.getOffscreenTexture(image_index);
    image_info.imageView = renderResult.getImageView();
    image_info.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet descriptor_image_writer{};
    descriptor_image_writer.dstSet = raytracingDescriptorSet[image_index];
    descriptor_image_writer.dstBinding = OUT_IMAGE_BINDING;
    descriptor_image_writer.dstArrayElement = 0;
    descriptor_image_writer.descriptorCount = 1;
    descriptor_image_writer.descriptorType = vk::DescriptorType::eStorageImage;
    descriptor_image_writer.pImageInfo = &image_info;

    std::vector<vk::WriteDescriptorSet> write_descriptor_sets = { write_descriptor_set_acceleration_structure,
        descriptor_image_writer };

    device->getLogicalDevice().updateDescriptorSets(write_descriptor_sets, {});
}

bool Kataglyphis::VulkanRenderer::record_commands(uint32_t image_index)
{
    if (image_index >= command_buffers.size() || image_index >= sharedRenderDescriptorSet.size()
        || image_index >= post_descriptor_set.size()) {
        spdlog::error(fmt::format("Command recording index out of range: {}", image_index));
        return false;
    }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    GUISceneSharedVars &guiSceneSharedVars = scene->getGuiSceneSharedVars();

    vk::CommandBuffer &commandBuffer = command_buffers[image_index];

    std::vector<vk::DescriptorSet> rasterizer_descriptor_sets = { sharedRenderDescriptorSet[image_index] };

    if (guiSceneSharedVars.clouds_enabled) {
        clouds.recordComputeCommands(commandBuffer, image_index, rasterizer_descriptor_sets);
    }

    if (guiSceneSharedVars.shadows_enabled) {
        dirShadowMap.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets);
    }

    if (guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward) {
        rasterizer.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets);
    } else {
        std::vector<vk::DescriptorSet> deferred_sets = { sharedRenderDescriptorSet[image_index], gbuffer_descriptor_set[image_index] };
        deferredRasterizer.recordCommands(commandBuffer, image_index, scene, deferred_sets);
    }

    if (device->supportsHardwareAcceleratedRRT() && image_index < raytracingDescriptorSet.size()) {
        std::vector<vk::DescriptorSet> raytracing_descriptor_sets = { sharedRenderDescriptorSet[image_index],
            raytracingDescriptorSet[image_index] };

        if (guiRendererSharedVars.raytracing) {
            Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(image_index) : deferredRasterizer.getOffscreenTexture(image_index);
            raytracingStage.recordCommands(
              commandBuffer, renderResult.getVulkanImage(), &vulkanSwapChain, raytracing_descriptor_sets);
        } else if (guiRendererSharedVars.pathTracing) {
            Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(image_index) : deferredRasterizer.getOffscreenTexture(image_index);
            pathTracing.recordCommands(
              commandBuffer, image_index, renderResult.getVulkanImage(), &vulkanSwapChain, raytracing_descriptor_sets);
        }
    }

    skyBox.recordCommands(commandBuffer, image_index, rasterizer_descriptor_sets, guiSceneSharedVars.skybox_enabled);

    vk::ImageMemoryBarrier colorBarrier{};
    colorBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    colorBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;
    colorBarrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.image = vulkanSwapChain.getSwapChainImage(image_index).getImage();
    colorBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    colorBarrier.subresourceRange.baseMipLevel = 0;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.baseArrayLayer = 0;
    colorBarrier.subresourceRange.layerCount = 1;
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::DependencyFlags{}, {}, {}, colorBarrier);

    std::vector<vk::DescriptorSet> post_descriptor_sets = { post_descriptor_set[image_index] };
    postStage.recordCommands(commandBuffer, image_index, post_descriptor_sets, guiSceneSharedVars.clouds_enabled, guiSceneSharedVars.shadows_enabled, guiSceneSharedVars.skybox_enabled);

    return true;
}

void Kataglyphis::VulkanRenderer::cleanUpUBOs()
{
    for (size_t i = 0; i < globalUBOBuffer.size(); i++) {
        device->getLogicalDevice().unmapMemory(globalUBOBuffer[i].getBufferMemory());
        globalUBOBuffer[i].cleanUp();
    }
    for (size_t i = 0; i < sceneUBOBuffer.size(); i++) {
        device->getLogicalDevice().unmapMemory(sceneUBOBuffer[i].getBufferMemory());
        sceneUBOBuffer[i].cleanUp();
    }
    globalUBOBuffer.clear();
    globalUBOMapped.clear();
    sceneUBOBuffer.clear();
    sceneUBOMapped.clear();
}

void Kataglyphis::VulkanRenderer::cleanUp()
{
    if (!device) { return; }

    std::ignore = device->getLogicalDevice().waitIdle();

    if (device->supportsHardwareAcceleratedRRT()) {
        pathTracing.cleanUp();
        raytracingStage.cleanUp();
        asManager.cleanUp();
    }

    rasterizer.cleanUp();
    deferredRasterizer.cleanUp();
    skyBox.cleanUp();
    clouds.cleanUp();
    dirShadowMap.cleanUp();
    pointShadowMap.cleanUp();
    postStage.cleanUp();

    objectDescriptionBuffer.cleanUp();

    cleanUpSync();
    cleanUpUBOs();
    cleanUpCommandPools();
    cleanUpDescriptorResources();

    if (raytracingDescriptorPool) {
        device->getLogicalDevice().destroyDescriptorPool(raytracingDescriptorPool);
        raytracingDescriptorPool = nullptr;
    }
    if (raytracingDescriptorSetLayout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(raytracingDescriptorSetLayout);
        raytracingDescriptorSetLayout = nullptr;
    }

    vulkanSwapChain.cleanUp();
    allocator.cleanUp();
    device->cleanUp();
    device.reset();

    if (surface) {
        instance.getVulkanInstance().destroySurfaceKHR(surface);
        surface = nullptr;
    }

    if (Kataglyphis::ENABLE_VALIDATION_LAYERS) { debug::freeDebugCallback(instance.getVulkanInstance()); }
    instance.cleanUp();
}

Kataglyphis::VulkanRenderer::~VulkanRenderer() { cleanUp(); }

void Kataglyphis::VulkanRenderer::create_surface()
{
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    ASSERT_VULKAN(glfwCreateWindowSurface(instance.getVulkanInstance(), window->get_window(), nullptr, &rawSurface),
      "Failed to create a surface!");
    surface = vk::SurfaceKHR(rawSurface);
}

void Kataglyphis::VulkanRenderer::create_post_descriptor_layout()
{
    vk::DescriptorSetLayoutBinding post_sampler_layout_binding{};
    post_sampler_layout_binding.binding = 0;
    post_sampler_layout_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    post_sampler_layout_binding.descriptorCount = 1;
    post_sampler_layout_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    post_sampler_layout_binding.pImmutableSamplers = nullptr;

    vk::DescriptorSetLayoutBinding cloud_sampler_layout_binding{};
    cloud_sampler_layout_binding.binding = 1;
    cloud_sampler_layout_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    cloud_sampler_layout_binding.descriptorCount = 1;
    cloud_sampler_layout_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    cloud_sampler_layout_binding.pImmutableSamplers = nullptr;

    std::vector<vk::DescriptorSetLayoutBinding> layout_bindings = { post_sampler_layout_binding, cloud_sampler_layout_binding };

    vk::DescriptorSetLayoutCreateInfo layout_create_info{};
    layout_create_info.bindingCount = static_cast<uint32_t>(layout_bindings.size());
    layout_create_info.pBindings = layout_bindings.data();

    auto result = device->getLogicalDevice().createDescriptorSetLayout(layout_create_info);
    if (result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create post descriptor set layout!");
        return;
    }
    post_descriptor_set_layout = result.value;

    vk::DescriptorPoolSize post_pool_size{};
    post_pool_size.type = vk::DescriptorType::eCombinedImageSampler;
    post_pool_size.descriptorCount = vulkanSwapChain.getNumberSwapChainImages() * 2; // 2 samplers per image

    std::vector<vk::DescriptorPoolSize> descriptor_pool_sizes = { post_pool_size };

    vk::DescriptorPoolCreateInfo pool_create_info{};
    pool_create_info.maxSets = vulkanSwapChain.getNumberSwapChainImages();
    pool_create_info.poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes.size());
    pool_create_info.pPoolSizes = descriptor_pool_sizes.data();

    auto pool_result = device->getLogicalDevice().createDescriptorPool(pool_create_info);
    if (pool_result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create post descriptor pool!");
        return;
    }
    post_descriptor_pool = pool_result.value;

    post_descriptor_set.resize(vulkanSwapChain.getNumberSwapChainImages());

    std::vector<vk::DescriptorSetLayout> set_layouts(
      vulkanSwapChain.getNumberSwapChainImages(), post_descriptor_set_layout);

    vk::DescriptorSetAllocateInfo set_alloc_info{};
    set_alloc_info.descriptorPool = post_descriptor_pool;
    set_alloc_info.descriptorSetCount = vulkanSwapChain.getNumberSwapChainImages();
    set_alloc_info.pSetLayouts = set_layouts.data();

    auto alloc_result = device->getLogicalDevice().allocateDescriptorSets(set_alloc_info);
    if (alloc_result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to allocate post descriptor sets!");
        post_descriptor_set.clear();
        return;
    }
    post_descriptor_set = alloc_result.value;
}

void Kataglyphis::VulkanRenderer::updatePostDescriptorSets()
{
    if (post_descriptor_set.size() < vulkanSwapChain.getNumberSwapChainImages()) {
        spdlog::error("Post descriptor sets are not available; skipping update.");
        return;
    }

    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vk::DescriptorImageInfo image_info{};
        image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();
        Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(static_cast<uint32_t>(i)) : deferredRasterizer.getOffscreenTexture(static_cast<uint32_t>(i));
        image_info.imageView = renderResult.getImageView();
        image_info.sampler = postStage.getOffscreenSampler();

        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write.dstSet = post_descriptor_set[i];
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo = &image_info;

        vk::DescriptorImageInfo cloud_info{};
        cloud_info.imageLayout = vk::ImageLayout::eGeneral;
        cloud_info.imageView = clouds.getCloudOutputTexture()->getImageView();
        cloud_info.sampler = clouds.getCloudOutputTexture()->getSampler();

        vk::WriteDescriptorSet cloud_write{};
        cloud_write.dstSet = post_descriptor_set[i];
        cloud_write.dstBinding = 1;
        cloud_write.dstArrayElement = 0;
        cloud_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        cloud_write.descriptorCount = 1;
        cloud_write.pImageInfo = &cloud_info;

        std::array<vk::WriteDescriptorSet, 2> writes = { descriptor_write, cloud_write };
        device->getLogicalDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void Kataglyphis::VulkanRenderer::createRaytracingDescriptorPool()
{
    std::array<vk::DescriptorPoolSize, 2> descriptor_pool_sizes{};
    const uint32_t swapchain_image_count = vulkanSwapChain.getNumberSwapChainImages();

    descriptor_pool_sizes[0].type = vk::DescriptorType::eAccelerationStructureKHR;
    descriptor_pool_sizes[0].descriptorCount = swapchain_image_count;

    descriptor_pool_sizes[1].type = vk::DescriptorType::eStorageImage;
    descriptor_pool_sizes[1].descriptorCount = swapchain_image_count;

    vk::DescriptorPoolCreateInfo descriptor_pool_create_info{};
    descriptor_pool_create_info.poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes.size());
    descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes.data();
    descriptor_pool_create_info.maxSets = swapchain_image_count;

    vk::Result const result =
      device->getLogicalDevice().createDescriptorPool(&descriptor_pool_create_info, nullptr, &raytracingDescriptorPool);
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create command pool!")
}

void Kataglyphis::VulkanRenderer::cleanUpSync()
{
    // Iterate each vector on its own: after a partial createSynchronization
    // failure their lengths can differ, and a shared loop bound would leak
    // whatever the longer vector still holds.
    for (vk::Semaphore semaphore : render_finished_by_image) {
        if (semaphore) { device->getLogicalDevice().destroySemaphore(semaphore); }
    }
    render_finished_by_image.clear();

    for (vk::Semaphore semaphore : image_available) {
        if (semaphore) { device->getLogicalDevice().destroySemaphore(semaphore); }
    }
    image_available.clear();

    for (vk::Fence fence : in_flight_fences) {
        if (fence) { device->getLogicalDevice().destroyFence(fence); }
    }
    in_flight_fences.clear();
    images_in_flight_fences.clear();
}

void Kataglyphis::VulkanRenderer::create_object_description_buffer()
{
    std::vector<ObjectDescription> objectDescriptions = scene->getObjectDescriptions();

    if (!objectDescriptions.empty()) {
        vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
          graphics_command_pool,
          objectDescriptionBuffer,
          vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eDeviceLocal,
          objectDescriptions);
    } else {
        // Create an empty buffer (1 byte) if no object descriptions are present to avoid validation error
        vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
          graphics_command_pool,
          objectDescriptionBuffer,
          vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eDeviceLocal,
          std::vector<uint32_t>{0});
    }

    updateObjectDescriptionDescriptorSets();
}

void Kataglyphis::VulkanRenderer::updateObjectDescriptionDescriptorSets()
{
    if (!objectDescriptionBuffer.getBuffer()) { return; }

    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        if (i >= sharedRenderDescriptorSet.size()) { break; }

        vk::DescriptorBufferInfo object_descriptions_buffer_info{};
        object_descriptions_buffer_info.buffer = objectDescriptionBuffer.getBuffer();
        object_descriptions_buffer_info.offset = 0;
        object_descriptions_buffer_info.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet descriptor_object_descriptions_writer{};
        descriptor_object_descriptions_writer.pNext = nullptr;
        descriptor_object_descriptions_writer.dstSet = sharedRenderDescriptorSet[i];
        descriptor_object_descriptions_writer.dstBinding = OBJECT_DESCRIPTION_BINDING;
        descriptor_object_descriptions_writer.dstArrayElement = 0;
        descriptor_object_descriptions_writer.descriptorCount = 1;
        descriptor_object_descriptions_writer.descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptor_object_descriptions_writer.pImageInfo = nullptr;
        descriptor_object_descriptions_writer.pBufferInfo = &object_descriptions_buffer_info;
        descriptor_object_descriptions_writer.pTexelBufferView = nullptr;

        std::vector<vk::WriteDescriptorSet> write_descriptor_sets = { descriptor_object_descriptions_writer };

        device->getLogicalDevice().updateDescriptorSets(
          static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
    }
}

void Kataglyphis::VulkanRenderer::createRaytracingDescriptorSetLayouts()
{
    {
        std::array<vk::DescriptorSetLayoutBinding, 2> descriptor_set_layout_bindings{};

        descriptor_set_layout_bindings[0].binding = TLAS_BINDING;
        descriptor_set_layout_bindings[0].descriptorCount = 1;
        descriptor_set_layout_bindings[0].descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
        descriptor_set_layout_bindings[0].pImmutableSamplers = nullptr;
        descriptor_set_layout_bindings[0].stageFlags = vk::ShaderStageFlagBits::eRaygenKHR
                                                       | vk::ShaderStageFlagBits::eClosestHitKHR
                                                       | vk::ShaderStageFlagBits::eCompute;

        descriptor_set_layout_bindings[1].binding = OUT_IMAGE_BINDING;
        descriptor_set_layout_bindings[1].descriptorCount = 1;
        descriptor_set_layout_bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        descriptor_set_layout_bindings[1].pImmutableSamplers = nullptr;
        descriptor_set_layout_bindings[1].stageFlags = vk::ShaderStageFlagBits::eRaygenKHR
                                                       | vk::ShaderStageFlagBits::eClosestHitKHR
                                                       | vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{};
        descriptor_set_layout_create_info.bindingCount = static_cast<uint32_t>(descriptor_set_layout_bindings.size());
        descriptor_set_layout_create_info.pBindings = descriptor_set_layout_bindings.data();

        vk::Result const result = device->getLogicalDevice().createDescriptorSetLayout(
          &descriptor_set_layout_create_info, nullptr, &raytracingDescriptorSetLayout);
        ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create raytracing descriptor set layout!")
    }
}

void Kataglyphis::VulkanRenderer::createRaytracingDescriptorSets()
{
    raytracingDescriptorSet.resize(vulkanSwapChain.getNumberSwapChainImages());

    std::vector<vk::DescriptorSetLayout> set_layouts(
      vulkanSwapChain.getNumberSwapChainImages(), raytracingDescriptorSetLayout);

    vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{};
    descriptor_set_allocate_info.descriptorPool = raytracingDescriptorPool;
    descriptor_set_allocate_info.descriptorSetCount = vulkanSwapChain.getNumberSwapChainImages();
    descriptor_set_allocate_info.pSetLayouts = set_layouts.data();

    vk::Result const result =
      device->getLogicalDevice().allocateDescriptorSets(&descriptor_set_allocate_info, raytracingDescriptorSet.data());
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to allocate raytracing descriptor set!")
}

void Kataglyphis::VulkanRenderer::updateRaytracingDescriptorSets()
{
    vk::AccelerationStructureKHR &vulkanTLAS = asManager.getTLAS();
    if (!vulkanTLAS) {
        return;
    }

    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vk::WriteDescriptorSetAccelerationStructureKHR descriptor_set_acceleration_structure{};
        descriptor_set_acceleration_structure.pNext = nullptr;
        descriptor_set_acceleration_structure.accelerationStructureCount = 1;
        descriptor_set_acceleration_structure.pAccelerationStructures = &vulkanTLAS;

        vk::WriteDescriptorSet write_descriptor_set_acceleration_structure{};
        write_descriptor_set_acceleration_structure.pNext = &descriptor_set_acceleration_structure;
        write_descriptor_set_acceleration_structure.dstSet = raytracingDescriptorSet[i];
        write_descriptor_set_acceleration_structure.dstBinding = TLAS_BINDING;
        write_descriptor_set_acceleration_structure.dstArrayElement = 0;
        write_descriptor_set_acceleration_structure.descriptorCount = 1;
        write_descriptor_set_acceleration_structure.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
        write_descriptor_set_acceleration_structure.pImageInfo = nullptr;
        write_descriptor_set_acceleration_structure.pBufferInfo = nullptr;
        write_descriptor_set_acceleration_structure.pTexelBufferView = nullptr;

        vk::DescriptorImageInfo image_info{};
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();
        Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(static_cast<uint32_t>(i)) : deferredRasterizer.getOffscreenTexture(static_cast<uint32_t>(i));
        image_info.imageView = renderResult.getImageView();
        image_info.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet descriptor_image_writer{};
        descriptor_image_writer.pNext = nullptr;
        descriptor_image_writer.dstSet = raytracingDescriptorSet[i];
        descriptor_image_writer.dstBinding = OUT_IMAGE_BINDING;
        descriptor_image_writer.dstArrayElement = 0;
        descriptor_image_writer.descriptorCount = 1;
        descriptor_image_writer.descriptorType = vk::DescriptorType::eStorageImage;
        descriptor_image_writer.pImageInfo = &image_info;
        descriptor_image_writer.pBufferInfo = nullptr;
        descriptor_image_writer.pTexelBufferView = nullptr;

        std::vector<vk::WriteDescriptorSet> write_descriptor_sets = { write_descriptor_set_acceleration_structure,
            descriptor_image_writer };

        device->getLogicalDevice().updateDescriptorSets(
          static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
    }
}

void Kataglyphis::VulkanRenderer::createSharedRenderDescriptorSetLayouts()
{
    const bool raytracing_available = device->supportsHardwareAcceleratedRRT();

    vk::ShaderStageFlags global_ubo_stages = vk::ShaderStageFlagBits::eVertex;
    vk::ShaderStageFlags scene_ubo_stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    vk::ShaderStageFlags object_description_stages =
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    vk::ShaderStageFlags sampler_stages = vk::ShaderStageFlagBits::eFragment;
    vk::ShaderStageFlags textures_stages = vk::ShaderStageFlagBits::eFragment;

    if (raytracing_available) {
        global_ubo_stages |= vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute;
        scene_ubo_stages |= vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR
                            | vk::ShaderStageFlagBits::eCompute;
        object_description_stages |= vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eCompute;
        sampler_stages |= vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eCompute;
        textures_stages |= vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eCompute;
    }

    std::array<vk::DescriptorSetLayoutBinding, 5> descriptor_set_layout_bindings{};
    descriptor_set_layout_bindings[0].binding = globalUBO_BINDING;
    descriptor_set_layout_bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    descriptor_set_layout_bindings[0].descriptorCount = 1;
    descriptor_set_layout_bindings[0].stageFlags = global_ubo_stages;
    descriptor_set_layout_bindings[0].pImmutableSamplers = nullptr;

    descriptor_set_layout_bindings[1].binding = sceneUBO_BINDING;
    descriptor_set_layout_bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
    descriptor_set_layout_bindings[1].descriptorCount = 1;
    descriptor_set_layout_bindings[1].stageFlags = scene_ubo_stages;
    descriptor_set_layout_bindings[1].pImmutableSamplers = nullptr;

    descriptor_set_layout_bindings[2].binding = OBJECT_DESCRIPTION_BINDING;
    descriptor_set_layout_bindings[2].descriptorCount = 1;
    descriptor_set_layout_bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
    descriptor_set_layout_bindings[2].pImmutableSamplers = nullptr;
    descriptor_set_layout_bindings[2].stageFlags = object_description_stages;

    descriptor_set_layout_bindings[3].binding = TEXTURES_BINDING;
    descriptor_set_layout_bindings[3].descriptorType = vk::DescriptorType::eSampledImage;
    descriptor_set_layout_bindings[3].descriptorCount = MAX_TEXTURE_COUNT;
    descriptor_set_layout_bindings[3].stageFlags = textures_stages;
    descriptor_set_layout_bindings[3].pImmutableSamplers = nullptr;

    descriptor_set_layout_bindings[4].binding = SAMPLER_BINDING;
    descriptor_set_layout_bindings[4].descriptorType = vk::DescriptorType::eSampler;
    descriptor_set_layout_bindings[4].descriptorCount = MAX_TEXTURE_COUNT;
    descriptor_set_layout_bindings[4].stageFlags = sampler_stages;
    descriptor_set_layout_bindings[4].pImmutableSamplers = nullptr;

    vk::DescriptorSetLayoutCreateInfo layout_create_info{};
    layout_create_info.bindingCount = static_cast<uint32_t>(descriptor_set_layout_bindings.size());
    layout_create_info.pBindings = descriptor_set_layout_bindings.data();

    auto result = device->getLogicalDevice().createDescriptorSetLayout(layout_create_info);
    if (result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create shared render descriptor set layout!");
        return;
    }
    sharedRenderDescriptorSetLayout = result.value;
}

void Kataglyphis::VulkanRenderer::create_command_pool()
{
    Kataglyphis::VulkanRendererInternals::QueueFamilyIndices const queue_family_indices = device->getQueueFamilies();

    {
        vk::CommandPoolCreateInfo pool_info{};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = static_cast<uint32_t>(queue_family_indices.graphics_family);

        vk::Result const result =
          device->getLogicalDevice().createCommandPool(&pool_info, nullptr, &graphics_command_pool);
        if (result != vk::Result::eSuccess) {
            spdlog::error("Failed to create graphics command pool! Error: {}", static_cast<int>(result));
            std::abort();
        }
    }

    {
        vk::CommandPoolCreateInfo pool_info{};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = static_cast<uint32_t>(queue_family_indices.compute_family);

        vk::Result const result =
          device->getLogicalDevice().createCommandPool(&pool_info, nullptr, &compute_command_pool);
        if (result != vk::Result::eSuccess) {
            spdlog::error("Failed to create compute command pool! Error: {}", static_cast<int>(result));
            std::abort();
        }
    }
}

void Kataglyphis::VulkanRenderer::cleanUpCommandPools()
{
    if (graphics_command_pool) {
        device->getLogicalDevice().destroyCommandPool(graphics_command_pool);
        graphics_command_pool = nullptr;
    }
    if (compute_command_pool) {
        device->getLogicalDevice().destroyCommandPool(compute_command_pool);
        compute_command_pool = nullptr;
    }
}

void Kataglyphis::VulkanRenderer::create_command_buffers()
{
    if (!command_buffers.empty()) {
        device->getLogicalDevice().freeCommandBuffers(graphics_command_pool, command_buffers);
    }
    command_buffers.resize(vulkanSwapChain.getNumberSwapChainImages());

    vk::CommandBufferAllocateInfo command_buffer_alloc_info{};
    command_buffer_alloc_info.commandPool = graphics_command_pool;
    command_buffer_alloc_info.level = vk::CommandBufferLevel::ePrimary;

    command_buffer_alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers.size());

    vk::Result const result =
      device->getLogicalDevice().allocateCommandBuffers(&command_buffer_alloc_info, command_buffers.data());
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to allocate command buffers!")
}

void Kataglyphis::VulkanRenderer::createSynchronization()
{
    frame_sync_count = std::min<uint32_t>(
      static_cast<uint32_t>(Kataglyphis::MAX_FRAME_DRAWS), vulkanSwapChain.getNumberSwapChainImages());

    cleanUpSync();

    image_available.resize(frame_sync_count);
    render_finished_by_image.resize(vulkanSwapChain.getNumberSwapChainImages());
    in_flight_fences.resize(frame_sync_count);
    images_in_flight_fences.resize(vulkanSwapChain.getNumberSwapChainImages());

    vk::SemaphoreCreateInfo semaphore_create_info{};

    vk::FenceCreateInfo fence_create_info{};
    fence_create_info.flags = vk::FenceCreateFlagBits::eSignaled;

    for (uint32_t i = 0; i < frame_sync_count; i++) {
        auto image_available_result_value = device->getLogicalDevice().createSemaphore(semaphore_create_info);
        auto image_available_result = image_available_result_value.result;
        auto image_available_handle = image_available_result_value.value;
        auto in_flight_fence_result_value = device->getLogicalDevice().createFence(fence_create_info);
        auto in_flight_fence_result = in_flight_fence_result_value.result;
        auto in_flight_fence_handle = in_flight_fence_result_value.value;

        if (image_available_result != vk::Result::eSuccess || in_flight_fence_result != vk::Result::eSuccess
            || !image_available_handle || !in_flight_fence_handle) {
            spdlog::error(
              fmt::format("Failed to create synchronization objects for frame {} (imageAvailable={}, fence={}).",
                i,
                static_cast<int>(image_available_result),
                static_cast<int>(in_flight_fence_result)));
            frame_sync_count = 0;
            return;
        }

        image_available[i] = image_available_handle;
        in_flight_fences[i] = in_flight_fence_handle;
    }

    for (uint32_t image = 0; image < vulkanSwapChain.getNumberSwapChainImages(); ++image) {
        auto render_finished_result_value = device->getLogicalDevice().createSemaphore(semaphore_create_info);
        auto render_finished_result = render_finished_result_value.result;
        auto render_finished_handle = render_finished_result_value.value;

        if (render_finished_result != vk::Result::eSuccess || !render_finished_handle) {
            spdlog::error(fmt::format("Failed to create render-finished semaphore for swapchain image {} ({}).",
              image,
              static_cast<int>(render_finished_result)));
            frame_sync_count = 0;
            return;
        }

        render_finished_by_image[image] = render_finished_handle;
    }

    for (uint32_t image = 0; image < vulkanSwapChain.getNumberSwapChainImages(); ++image) {
        images_in_flight_fences[image] = nullptr;
    }

    current_frame = 0;
}

void Kataglyphis::VulkanRenderer::create_uniform_buffers()
{
    const uint32_t imageCount = vulkanSwapChain.getNumberSwapChainImages();
    globalUBOBuffer.resize(imageCount);
    globalUBOMapped.resize(imageCount);
    sceneUBOBuffer.resize(imageCount);
    sceneUBOMapped.resize(imageCount);

    for (size_t i = 0; i < imageCount; i++) {
        globalUBOBuffer[i].create(device,
          sizeof(VulkanRendererInternals::GlobalUBO),
          vk::BufferUsageFlagBits::eUniformBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        
        globalUBOMapped[i] = device->getLogicalDevice().mapMemory(globalUBOBuffer[i].getBufferMemory(), 0, sizeof(VulkanRendererInternals::GlobalUBO)).value;

        sceneUBOBuffer[i].create(device,
          sizeof(VulkanRendererInternals::SceneUBO),
          vk::BufferUsageFlagBits::eUniformBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        sceneUBOMapped[i] = device->getLogicalDevice().mapMemory(sceneUBOBuffer[i].getBufferMemory(), 0, sizeof(VulkanRendererInternals::SceneUBO)).value;
        
        // Initial upload
        update_uniform_buffers(static_cast<uint32_t>(i));
    }
}

void Kataglyphis::VulkanRenderer::createDescriptorPoolSharedRenderStages()
{
    vk::DescriptorPoolSize vp_pool_size{};
    vp_pool_size.type = vk::DescriptorType::eUniformBuffer;
    vp_pool_size.descriptorCount = static_cast<uint32_t>(globalUBOBuffer.size());

    vk::DescriptorPoolSize directions_pool_size{};
    directions_pool_size.type = vk::DescriptorType::eUniformBuffer;
    directions_pool_size.descriptorCount = static_cast<uint32_t>(sceneUBOBuffer.size());

    vk::DescriptorPoolSize object_descriptions_pool_size{};
    object_descriptions_pool_size.type = vk::DescriptorType::eStorageBuffer;
    object_descriptions_pool_size.descriptorCount =
      static_cast<uint32_t>(sizeof(ObjectDescription) * Kataglyphis::MAX_OBJECTS);

    vk::DescriptorPoolSize sampler_pool_size{};
    sampler_pool_size.type = vk::DescriptorType::eSampler;
    sampler_pool_size.descriptorCount = MAX_TEXTURE_COUNT * vulkanSwapChain.getNumberSwapChainImages();

    vk::DescriptorPoolSize sampled_image_pool_size{};
    sampled_image_pool_size.type = vk::DescriptorType::eSampledImage;
    sampled_image_pool_size.descriptorCount = MAX_TEXTURE_COUNT * vulkanSwapChain.getNumberSwapChainImages();

    std::vector<vk::DescriptorPoolSize> descriptor_pool_sizes = {
        vp_pool_size, directions_pool_size, object_descriptions_pool_size, sampler_pool_size, sampled_image_pool_size
    };

    vk::DescriptorPoolCreateInfo pool_create_info{};
    pool_create_info.maxSets = vulkanSwapChain.getNumberSwapChainImages();
    pool_create_info.poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes.size());
    pool_create_info.pPoolSizes = descriptor_pool_sizes.data();

    vk::Result const result =
      device->getLogicalDevice().createDescriptorPool(&pool_create_info, nullptr, &descriptorPoolSharedRenderStages);
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create a descriptor pool!")
}

void Kataglyphis::VulkanRenderer::createSharedRenderDescriptorSet()
{
    sharedRenderDescriptorSet.resize(vulkanSwapChain.getNumberSwapChainImages());

    std::vector<vk::DescriptorSetLayout> set_layouts(
      vulkanSwapChain.getNumberSwapChainImages(), sharedRenderDescriptorSetLayout);

    vk::DescriptorSetAllocateInfo set_alloc_info{};
    set_alloc_info.descriptorPool = descriptorPoolSharedRenderStages;
    set_alloc_info.descriptorSetCount = vulkanSwapChain.getNumberSwapChainImages();
    set_alloc_info.pSetLayouts = set_layouts.data();

    auto result = device->getLogicalDevice().allocateDescriptorSets(set_alloc_info);
    if (result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to allocate shared render descriptor sets!");
        sharedRenderDescriptorSet.clear();
        return;
    }
    sharedRenderDescriptorSet = result.value;

    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vk::DescriptorBufferInfo globalUBO_buffer_info{};
        globalUBO_buffer_info.buffer = globalUBOBuffer[i].getBuffer();
        globalUBO_buffer_info.offset = 0;
        globalUBO_buffer_info.range = sizeof(globalUBO);

        vk::WriteDescriptorSet globalUBO_set_write{};
        globalUBO_set_write.dstSet = sharedRenderDescriptorSet[i];
        globalUBO_set_write.dstBinding = 0;
        globalUBO_set_write.dstArrayElement = 0;
        globalUBO_set_write.descriptorType = vk::DescriptorType::eUniformBuffer;
        globalUBO_set_write.descriptorCount = 1;
        globalUBO_set_write.pBufferInfo = &globalUBO_buffer_info;

        vk::DescriptorBufferInfo sceneUBO_buffer_info{};
        sceneUBO_buffer_info.buffer = sceneUBOBuffer[i].getBuffer();
        sceneUBO_buffer_info.offset = 0;
        sceneUBO_buffer_info.range = sizeof(sceneUBO);

        vk::WriteDescriptorSet sceneUBO_set_write{};
        sceneUBO_set_write.dstSet = sharedRenderDescriptorSet[i];
        sceneUBO_set_write.dstBinding = 1;
        sceneUBO_set_write.dstArrayElement = 0;
        sceneUBO_set_write.descriptorType = vk::DescriptorType::eUniformBuffer;
        sceneUBO_set_write.descriptorCount = 1;
        sceneUBO_set_write.pBufferInfo = &sceneUBO_buffer_info;

        std::vector<vk::WriteDescriptorSet> write_descriptor_sets = { globalUBO_set_write, sceneUBO_set_write };

        device->getLogicalDevice().updateDescriptorSets(
          static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
    }
}

void Kataglyphis::VulkanRenderer::updateTexturesInSharedRenderDescriptorSet()
{
    if (sharedRenderDescriptorSet.empty()) {
        return;
    }

    if (scene->getModelCount() == 0) {
        return;
    }

    std::vector<Texture> &modelTextures = scene->getTextures(0);
    const uint32_t scene_texture_count = scene->getTextureCount(0);
    if (scene_texture_count == 0) {
        return;
    }

    const uint32_t texture_count_for_descriptors = std::min<uint32_t>(scene_texture_count, MAX_TEXTURE_COUNT);
    std::vector<vk::Sampler> &modelTextureSampler = scene->getTextureSampler(0);

    std::vector<vk::DescriptorImageInfo> image_info_textures(MAX_TEXTURE_COUNT);
    std::vector<vk::DescriptorImageInfo> image_info_texture_sampler(MAX_TEXTURE_COUNT);

    for (uint32_t i = 0; i < MAX_TEXTURE_COUNT; i++) {
        const uint32_t texture_index = (i < texture_count_for_descriptors) ? i : 0;
        
        image_info_textures[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image_info_textures[i].imageView = modelTextures[texture_index].getImageView();
        
        image_info_texture_sampler[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image_info_texture_sampler[i].sampler = modelTextureSampler[texture_index];
    }

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write.dstSet = sharedRenderDescriptorSet[i];
        descriptor_write.dstBinding = TEXTURES_BINDING;
        descriptor_write.descriptorType = vk::DescriptorType::eSampledImage;
        descriptor_write.descriptorCount = MAX_TEXTURE_COUNT;
        descriptor_write.pImageInfo = image_info_textures.data();

        vk::WriteDescriptorSet descriptor_sampler_write{};
        descriptor_sampler_write.dstSet = sharedRenderDescriptorSet[i];
        descriptor_sampler_write.dstBinding = SAMPLER_BINDING;
        descriptor_sampler_write.descriptorType = vk::DescriptorType::eSampler;
        descriptor_sampler_write.descriptorCount = MAX_TEXTURE_COUNT;
        descriptor_sampler_write.pImageInfo = image_info_texture_sampler.data();

        std::array<vk::WriteDescriptorSet, 2> write_descriptor_sets = { descriptor_write, descriptor_sampler_write };
        device->getLogicalDevice().updateDescriptorSets(write_descriptor_sets, nullptr);
    }
}
void Kataglyphis::VulkanRenderer::create_gbuffer_descriptor_layout()
{
    std::array<vk::DescriptorSetLayoutBinding, 5> layout_bindings{};
    for(uint32_t i = 0; i < 5; i++) {
        layout_bindings[i].binding = i;
        layout_bindings[i].descriptorType = vk::DescriptorType::eInputAttachment;
        layout_bindings[i].descriptorCount = 1;
        layout_bindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
        layout_bindings[i].pImmutableSamplers = nullptr;
    }

    vk::DescriptorSetLayoutCreateInfo layout_create_info{};
    layout_create_info.bindingCount = static_cast<uint32_t>(layout_bindings.size());
    layout_create_info.pBindings = layout_bindings.data();

    auto result = device->getLogicalDevice().createDescriptorSetLayout(layout_create_info);
    if (result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create gbuffer descriptor set layout!");
        return;
    }
    gbuffer_descriptor_set_layout = result.value;

    vk::DescriptorPoolSize pool_size{};
    pool_size.type = vk::DescriptorType::eInputAttachment;
    pool_size.descriptorCount = static_cast<uint32_t>(vulkanSwapChain.getNumberSwapChainImages()) * 5;

    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = static_cast<uint32_t>(vulkanSwapChain.getNumberSwapChainImages());

    auto pool_result = device->getLogicalDevice().createDescriptorPool(pool_info);
    if (pool_result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create gbuffer descriptor pool!");
        return;
    }
    gbuffer_descriptor_pool = pool_result.value;

    std::vector<vk::DescriptorSetLayout> layouts(vulkanSwapChain.getNumberSwapChainImages(), gbuffer_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo alloc_info{};
    alloc_info.descriptorPool = gbuffer_descriptor_pool;
    alloc_info.descriptorSetCount = static_cast<uint32_t>(vulkanSwapChain.getNumberSwapChainImages());
    alloc_info.pSetLayouts = layouts.data();

    auto alloc_result = device->getLogicalDevice().allocateDescriptorSets(alloc_info);
    if (alloc_result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to allocate gbuffer descriptor sets!");
        return;
    }
    gbuffer_descriptor_set = alloc_result.value;
}

void Kataglyphis::VulkanRenderer::updateGBufferDescriptorSets()
{
    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        std::array<vk::DescriptorImageInfo, 5> imageInfos;
        
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[0].imageView = deferredRasterizer.getGBufferPosition(static_cast<uint32_t>(i));
        
        imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[1].imageView = deferredRasterizer.getGBufferNormal(static_cast<uint32_t>(i));
        
        imageInfos[2].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[2].imageView = deferredRasterizer.getGBufferAlbedo(static_cast<uint32_t>(i));
        
        imageInfos[3].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[3].imageView = deferredRasterizer.getGBufferMaterial(static_cast<uint32_t>(i));
        
        imageInfos[4].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[4].imageView = deferredRasterizer.getDepthBufferImageView();

        std::array<vk::WriteDescriptorSet, 5> descriptorWrites;;
        for(uint32_t j = 0; j < 5; j++) {
            descriptorWrites[j].dstSet = gbuffer_descriptor_set[i];
            descriptorWrites[j].dstBinding = j;
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = vk::DescriptorType::eInputAttachment;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pImageInfo = &imageInfos[j];
        }

        device->getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }
}
