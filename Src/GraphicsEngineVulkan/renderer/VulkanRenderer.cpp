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
import kataglyphis.vulkan.descriptor_set_group;
import kataglyphis.vulkan.instance;
import kataglyphis.vulkan.gui;
import kataglyphis.vulkan.scene_ubo;
import kataglyphis.vulkan.global_ubo;
import kataglyphis.vulkan.swapchain;
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

    create_command_pool();

    vulkanSwapChain.initVulkanContext(device, window, surface);
    createGpuTimingResources();
    create_uniform_buffers();
    create_command_buffers();

    createSynchronization();

    initDescriptorResources();

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_rasterizer = { sharedRenderDescriptors.getLayout() };
    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_deferred = { sharedRenderDescriptors.getLayout(), gbufferDescriptors.getLayout() };

    rasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_rasterizer, graphics_command_pool);
    deferredRasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_deferred, graphics_command_pool);

    clouds.init(device, graphics_command_pool, sharedRenderDescriptors.getLayout(), vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);
    dirShadowMap.init(device, 2048, 2048, MAX_CASCADES);
    dirShadowMap.createGraphicsPipeline();
    pointShadowMap.init(device, 1024, 1024);

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_post = { postDescriptors.getLayout() };
    postStage.init(device, &vulkanSwapChain, descriptor_set_layouts_post);

    if (device->supportsHardwareAcceleratedRRT()) {
        createRaytracingDescriptorResources();

        std::vector<vk::DescriptorSetLayout> const layouts = { sharedRenderDescriptors.getLayout(),
            raytracingDescriptors.getLayout() };
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
    skyBox.createGraphicsPipeline(sharedRenderDescriptors.getLayout());
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

    // Inverses for the clouds compute shader (see GlobalUBO.hpp).
    globalUBO.inv_projection = glm::inverse(globalUBO.projection);
    globalUBO.inv_view = glm::inverse(globalUBO.view);

    const auto& cascadeData = dirShadowMap.getCascadeData();
    const size_t active_cascades = std::min(cascadeData.size(), static_cast<size_t>(MAX_CASCADES));
    for (size_t i = 0; i < active_cascades; ++i) {
        sceneUBO.cascadeSplits[static_cast<int>(i)] = cascadeData[i].splitDepth;
        sceneUBO.cascadeLightSpaceMatrices[i] = cascadeData[i].viewProjMatrix;
    }
    sceneUBO.numCascades = guiSceneSharedVars.shadows_enabled ? static_cast<unsigned int>(active_cascades) : 0U;

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
        // cleanUp() destroyed the pipeline, descriptor resources and the light
        // matrices buffer; recreate them (same sequence as at startup).
        dirShadowMap.createGraphicsPipeline();

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

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts = { sharedRenderDescriptors.getLayout() };
    rasterizer.shaderHotReload(descriptor_set_layouts);

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_post = { postDescriptors.getLayout() };
    postStage.shaderHotReload(descriptor_set_layouts_post);

    if (device->supportsHardwareAcceleratedRRT()) {
        std::vector<vk::DescriptorSetLayout> const layouts = { sharedRenderDescriptors.getLayout(),
            raytracingDescriptors.getLayout() };
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

    // The fence wait above guarantees the previous commands that used this
    // swapchain image (and its query slice) completed, so the readback below
    // never has to wait on the GPU.
    readGpuTimings(image_index);

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

    // A capture recorded into this command buffer completes when this frame's
    // in-flight fence signals; takeCapturedFrame() waits on exactly that.
    if (capture_pending) { capture_fence = in_flight_fences[current_frame]; }

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

    // createSynchronization() below destroys and recreates every fence, so a
    // capture fence recorded against the old set must be dropped. The waitIdle
    // above already guarantees a pending capture's copy has completed, so the
    // staged pixels stay readable - only the (now dangling) fence goes away.
    capture_fence = vk::Fence{};

    uint32_t oldImageCount = vulkanSwapChain.getNumberSwapChainImages();

    // Destroy framebuffers that reference swapchain image views
    // before recreating the swapchain
    postStage.destroyFramebuffers();
    rasterizer.destroyFramebuffers();
    deferredRasterizer.destroyFramebuffers();
    skyBox.destroyFramebuffers();

    vulkanSwapChain.recreate(device, surface);

    // The query pool is sized per swapchain image; the waitIdle above makes
    // destroying and recreating it safe here.
    createGpuTimingResources();

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
            // Rebuilds layout + pool + sets. The recreated layout is
            // structurally identical, so the pipeline layouts created from
            // the old handle at init stay bind-compatible (same pattern the
            // shared/post/gbuffer groups already rely on).
            raytracingDescriptors.cleanUp();
            createRaytracingDescriptorResources();
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
    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        sharedRenderDescriptors.writeBuffer(i, globalUBO_BINDING, globalUBOBuffer[i].getBuffer(), sizeof(globalUBO));
        sharedRenderDescriptors.writeBuffer(i, sceneUBO_BINDING, sceneUBOBuffer[i].getBuffer(), sizeof(sceneUBO));
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
    sharedRenderDescriptors.cleanUp();
    postDescriptors.cleanUp();
    gbufferDescriptors.cleanUp();
}

void Kataglyphis::VulkanRenderer::initDescriptorResources()
{
    createSharedRenderDescriptorResources();
    create_post_descriptor_resources();
    create_gbuffer_descriptor_resources();
}

void Kataglyphis::VulkanRenderer::update_raytracing_descriptor_set(uint32_t image_index)
{
    if (image_index >= raytracingDescriptors.sets().size()) {
        spdlog::error(fmt::format("Raytracing descriptor set index out of range: {}", image_index));
        return;
    }

    vk::AccelerationStructureKHR &vulkanTLAS = asManager.getTLAS();
    if (!vulkanTLAS) {
        return;
    }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();
    Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(image_index) : deferredRasterizer.getOffscreenTexture(image_index);

    raytracingDescriptors.writeAccelerationStructure(image_index, TLAS_BINDING, vulkanTLAS);
    raytracingDescriptors.writeImage(image_index, OUT_IMAGE_BINDING, renderResult.getImageView(), vk::ImageLayout::eGeneral);
}

bool Kataglyphis::VulkanRenderer::record_commands(uint32_t image_index)
{
    if (image_index >= command_buffers.size() || image_index >= sharedRenderDescriptors.sets().size()
        || image_index >= postDescriptors.sets().size()) {
        spdlog::error(fmt::format("Command recording index out of range: {}", image_index));
        return false;
    }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    GUISceneSharedVars &guiSceneSharedVars = scene->getGuiSceneSharedVars();

    vk::CommandBuffer &commandBuffer = command_buffers[image_index];

    namespace FrontendShared = Kataglyphis::VulkanRendererInternals::FrontendShared;
    using FrontendShared::GpuTimedPass;

    // -- per-pass GPU timing: reset this image's query slice (outside any
    // render pass) and bracket every recorded pass with a timestamp pair.
    const bool record_gpu_timings =
      gpu_timings_supported && gpu_timing_query_pool && image_index < gpu_timing_pass_mask.size();
    const uint32_t gpu_timing_base = image_index * GPU_TIMING_QUERIES_PER_IMAGE;
    uint32_t recorded_pass_mask = 0U;

    if (record_gpu_timings) {
        commandBuffer.resetQueryPool(gpu_timing_query_pool, gpu_timing_base, GPU_TIMING_QUERIES_PER_IMAGE);
    }

    const auto write_pass_timestamp = [&](GpuTimedPass pass, bool start) -> void {
        if (!record_gpu_timings) { return; }
        const uint32_t pass_index = static_cast<uint32_t>(pass);
        const uint32_t query = gpu_timing_base + pass_index * GPU_TIMING_QUERIES_PER_PASS + (start ? 0U : 1U);
        commandBuffer.writeTimestamp(
          start ? vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eBottomOfPipe,
          gpu_timing_query_pool,
          query);
        if (!start) { recorded_pass_mask |= (1U << pass_index); }
    };

    std::vector<vk::DescriptorSet> rasterizer_descriptor_sets = { sharedRenderDescriptors.sets()[image_index] };

    if (guiSceneSharedVars.clouds_enabled) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "clouds", { 0.80F, 0.85F, 0.95F, 1.0F });
        write_pass_timestamp(GpuTimedPass::Clouds, true);
        clouds.recordComputeCommands(commandBuffer, image_index, rasterizer_descriptor_sets);
        write_pass_timestamp(GpuTimedPass::Clouds, false);
    }

    if (guiSceneSharedVars.shadows_enabled) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "shadow_cascades", { 0.55F, 0.35F, 0.10F, 1.0F });
        write_pass_timestamp(GpuTimedPass::ShadowCascades, true);
        dirShadowMap.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets);
        write_pass_timestamp(GpuTimedPass::ShadowCascades, false);
    }

    write_pass_timestamp(GpuTimedPass::Main, true);

    if (guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "forward", { 0.20F, 0.60F, 1.00F, 1.0F });
        rasterizer.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets);
    } else {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "deferred", { 0.20F, 0.40F, 0.80F, 1.0F });
        std::vector<vk::DescriptorSet> deferred_sets = { sharedRenderDescriptors.sets()[image_index], gbufferDescriptors.sets()[image_index] };
        deferredRasterizer.recordCommands(commandBuffer, image_index, scene, deferred_sets);
    }

    if (device->supportsHardwareAcceleratedRRT() && image_index < raytracingDescriptors.sets().size()) {
        std::vector<vk::DescriptorSet> raytracing_descriptor_sets = { sharedRenderDescriptors.sets()[image_index],
            raytracingDescriptors.sets()[image_index] };

        if (guiRendererSharedVars.raytracing) {
            Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "raytracing", { 0.85F, 0.25F, 0.55F, 1.0F });
            Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(image_index) : deferredRasterizer.getOffscreenTexture(image_index);
            raytracingStage.recordCommands(
              commandBuffer, renderResult.getVulkanImage(), &vulkanSwapChain, raytracing_descriptor_sets);
        } else if (guiRendererSharedVars.pathTracing) {
            Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "pathtracing", { 0.60F, 0.25F, 0.85F, 1.0F });
            Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(image_index) : deferredRasterizer.getOffscreenTexture(image_index);
            pathTracing.recordCommands(
              commandBuffer, image_index, renderResult.getVulkanImage(), &vulkanSwapChain, raytracing_descriptor_sets);
        }
    }

    write_pass_timestamp(GpuTimedPass::Main, false);

    write_pass_timestamp(GpuTimedPass::Sky, true);
    {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "sky", { 0.30F, 0.70F, 0.90F, 1.0F });
        skyBox.recordCommands(
          commandBuffer, image_index, rasterizer_descriptor_sets, guiSceneSharedVars.skybox_enabled);
    }
    write_pass_timestamp(GpuTimedPass::Sky, false);

    // NOTE: a same-layout swapchain barrier
    // (eColorAttachmentOptimal -> eColorAttachmentOptimal) used to sit here;
    // synchronization validation (khronos_validation.validate_sync) confirms
    // the post render pass's external dependency already covers the ordering.

    write_pass_timestamp(GpuTimedPass::Post, true);
    {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "post", { 0.35F, 0.80F, 0.40F, 1.0F });
        std::vector<vk::DescriptorSet> post_descriptor_sets = { postDescriptors.sets()[image_index] };
        postStage.recordCommands(commandBuffer, image_index, post_descriptor_sets, guiSceneSharedVars.clouds_enabled, guiSceneSharedVars.shadows_enabled, guiSceneSharedVars.skybox_enabled);
    }
    write_pass_timestamp(GpuTimedPass::Post, false);

    // The post render pass left the swapchain image in ePresentSrcKHR; capture
    // copies from it here, still inside this frame's command buffer, and
    // restores the layout before the present below.
    if (capture_armed) { recordFrameCapture(commandBuffer, image_index); }

    if (record_gpu_timings) {
        gpu_timing_pass_mask[image_index] = recorded_pass_mask;
        gpu_timing_slice_recorded[image_index] = true;
    }

    return true;
}

auto Kataglyphis::VulkanRenderer::supportsFrameCapture() const -> bool
{
    return device != nullptr && !device_lost_detected && vulkanSwapChain.supportsTransferSrc();
}

void Kataglyphis::VulkanRenderer::requestFrameCapture()
{
    if (!supportsFrameCapture()) {
        spdlog::warn("Frame capture requested but the surface does not support eTransferSrc; ignoring.");
        return;
    }

    capture_armed = true;
    capture_pending = false;
    capture_fence = vk::Fence{};
}

void Kataglyphis::VulkanRenderer::recordFrameCapture(vk::CommandBuffer &commandBuffer, uint32_t image_index)
{
    capture_armed = false;

    if (!supportsFrameCapture()) { return; }

    const vk::Extent2D extent = vulkanSwapChain.getSwapChainExtent();
    if (extent.width == 0 || extent.height == 0) { return; }

    const vk::DeviceSize required_size =
      static_cast<vk::DeviceSize>(extent.width) * static_cast<vk::DeviceSize>(extent.height) * 4ULL;

    // (Re)allocate the staging buffer only when the extent grew or it does not
    // exist yet. Safe here because a resize goes through recreateSwapChain(),
    // which waits idle and clears any pending capture.
    if (capture_buffer_size < required_size) {
        captureBuffer.cleanUp();
        captureBuffer.create(device,
          required_size,
          vk::BufferUsageFlagBits::eTransferDst,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        capture_buffer_size = required_size;
    }

    if (captureBuffer.getMappedData() == nullptr) {
        spdlog::error("Frame capture staging buffer is not host mapped; capture skipped.");
        return;
    }

    vk::Image &swapchain_image = vulkanSwapChain.getSwapChainImage(image_index).getImage();

    vk::ImageMemoryBarrier to_transfer_src{};
    to_transfer_src.oldLayout = vk::ImageLayout::ePresentSrcKHR;
    to_transfer_src.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    to_transfer_src.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_transfer_src.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_transfer_src.image = swapchain_image;
    to_transfer_src.subresourceRange = vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    to_transfer_src.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    to_transfer_src.dstAccessMask = vk::AccessFlagBits::eTransferRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::PipelineStageFlagBits::eTransfer,
      vk::DependencyFlags{},
      0,
      nullptr,
      0,
      nullptr,
      1,
      &to_transfer_src);

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;// tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource = vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
    region.imageOffset = vk::Offset3D{ 0, 0, 0 };
    region.imageExtent = vk::Extent3D{ extent.width, extent.height, 1 };

    commandBuffer.copyImageToBuffer(
      swapchain_image, vk::ImageLayout::eTransferSrcOptimal, captureBuffer.getBuffer(), 1, &region);

    // Restore the layout the present expects.
    vk::ImageMemoryBarrier back_to_present{};
    back_to_present.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
    back_to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
    back_to_present.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    back_to_present.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    back_to_present.image = swapchain_image;
    back_to_present.subresourceRange = vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    back_to_present.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    back_to_present.dstAccessMask = vk::AccessFlagBits{};

    // Make the copy visible to host reads of the staging buffer as well.
    vk::BufferMemoryBarrier buffer_to_host{};
    buffer_to_host.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    buffer_to_host.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    buffer_to_host.buffer = captureBuffer.getBuffer();
    buffer_to_host.offset = 0;
    buffer_to_host.size = required_size;
    buffer_to_host.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    buffer_to_host.dstAccessMask = vk::AccessFlagBits::eHostRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eBottomOfPipe | vk::PipelineStageFlagBits::eHost,
      vk::DependencyFlags{},
      0,
      nullptr,
      1,
      &buffer_to_host,
      1,
      &back_to_present);

    capture_width = extent.width;
    capture_height = extent.height;
    capture_format = vulkanSwapChain.getSwapChainFormat();
    capture_pending = true;
    // Set to the frame's in-flight fence by drawFrame right after the submit.
    capture_fence = vk::Fence{};
}

auto Kataglyphis::VulkanRenderer::takeCapturedFrame(uint32_t &outWidth, uint32_t &outHeight) -> std::vector<uint8_t>
{
    outWidth = 0;
    outHeight = 0;

    if (!capture_pending || device_lost_detected || !device) { return {}; }

    capture_pending = false;

    if (capture_fence) {
        const vk::Result wait_result = device->getLogicalDevice().waitForFences(
          1, &capture_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
        if (wait_result != vk::Result::eSuccess) {
            spdlog::error(fmt::format("Failed to wait for the frame capture fence (vk::Result={})",
              static_cast<int>(wait_result)));
            if (wait_result == vk::Result::eErrorDeviceLost) { device_lost_detected = true; }
            return {};
        }
    }

    const void *mapped = captureBuffer.getMappedData();
    if (mapped == nullptr || capture_width == 0 || capture_height == 0) { return {}; }

    const size_t pixel_count = static_cast<size_t>(capture_width) * static_cast<size_t>(capture_height);
    std::vector<uint8_t> pixels(pixel_count * 4U);
    std::memcpy(pixels.data(), mapped, pixels.size());

    // Normalize to RGBA8 regardless of the swapchain's channel order.
    const bool is_bgra = capture_format == vk::Format::eB8G8R8A8Unorm || capture_format == vk::Format::eB8G8R8A8Srgb
                         || capture_format == vk::Format::eB8G8R8A8Snorm
                         || capture_format == vk::Format::eB8G8R8A8Uint;
    if (is_bgra) {
        for (size_t i = 0; i < pixels.size(); i += 4U) { std::swap(pixels[i], pixels[i + 2U]); }
    }

    outWidth = capture_width;
    outHeight = capture_height;
    return pixels;
}

void Kataglyphis::VulkanRenderer::cleanUpFrameCapture()
{
    captureBuffer.cleanUp();
    capture_buffer_size = 0;
    capture_armed = false;
    capture_pending = false;
    capture_fence = vk::Fence{};
    capture_width = 0;
    capture_height = 0;
}

void Kataglyphis::VulkanRenderer::createGpuTimingResources()
{
    destroyGpuTimingResources();

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    const uint32_t valid_bits = device->getGraphicsQueueTimestampValidBits();
    gpu_timestamp_period = device->getTimestampPeriod();
    gpu_timings_supported = (valid_bits != 0U) && (gpu_timestamp_period > 0.0F);

    guiRendererSharedVars.gpuTimings.supported = gpu_timings_supported;
    for (float &pass_ms : guiRendererSharedVars.gpuTimings.pass_ms) { pass_ms = -1.0F; }
    for (auto &average : gpu_pass_averages) { average.reset(); }

    if (!gpu_timings_supported) {
        spdlog::info(
          "GPU timestamps are not supported on the graphics queue family (timestampValidBits == 0); "
          "per-pass GPU timings disabled.");
        return;
    }

    gpu_timestamp_mask = (valid_bits >= 64U) ? ~0ULL : ((1ULL << valid_bits) - 1ULL);

    const uint32_t image_count = vulkanSwapChain.getNumberSwapChainImages();

    vk::QueryPoolCreateInfo query_pool_info{};
    query_pool_info.queryType = vk::QueryType::eTimestamp;
    query_pool_info.queryCount = GPU_TIMING_QUERIES_PER_IMAGE * image_count;

    auto pool_result = device->getLogicalDevice().createQueryPool(query_pool_info);
    if (pool_result.result != vk::Result::eSuccess) {
        spdlog::warn("Failed to create the GPU timing query pool (result {}); per-pass GPU timings disabled.",
          static_cast<int>(pool_result.result));
        gpu_timings_supported = false;
        guiRendererSharedVars.gpuTimings.supported = false;
        return;
    }
    gpu_timing_query_pool = pool_result.value;
    gpu_timing_pass_mask.assign(image_count, 0U);
    gpu_timing_slice_recorded.assign(image_count, false);
}

void Kataglyphis::VulkanRenderer::destroyGpuTimingResources()
{
    if (gpu_timing_query_pool) {
        device->getLogicalDevice().destroyQueryPool(gpu_timing_query_pool);
        gpu_timing_query_pool = nullptr;
    }
    gpu_timing_pass_mask.clear();
    gpu_timing_slice_recorded.clear();
}

void Kataglyphis::VulkanRenderer::readGpuTimings(uint32_t image_index)
{
    namespace FrontendShared = Kataglyphis::VulkanRendererInternals::FrontendShared;

    FrontendShared::GUIRendererSharedVars &guiRendererSharedVars = gui->getGuiRendererSharedVars();
    guiRendererSharedVars.gpuTimings.supported = gpu_timings_supported;

    if (!gpu_timings_supported || !gpu_timing_query_pool) { return; }
    // Freshly created pools hold queries in an undefined state; only read a
    // slice after it was reset and written at least once.
    if (image_index >= gpu_timing_slice_recorded.size() || !gpu_timing_slice_recorded[image_index]) { return; }

    // Two uint64 per query: [value, availability].
    std::array<uint64_t, static_cast<size_t>(GPU_TIMING_QUERIES_PER_IMAGE) * 2U> query_data{};

    // Deliberately WITHOUT eWait: an unavailable result must be skipped, never
    // stalled on. eWithAvailability reports per-query availability.
    const vk::Result result = device->getLogicalDevice().getQueryPoolResults(gpu_timing_query_pool,
      image_index * GPU_TIMING_QUERIES_PER_IMAGE,
      GPU_TIMING_QUERIES_PER_IMAGE,
      sizeof(query_data),
      query_data.data(),
      2U * sizeof(uint64_t),
      vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) { return; }

    const uint32_t recorded_passes = gpu_timing_pass_mask[image_index];
    constexpr double NANOSECONDS_PER_MILLISECOND = 1.0e6;

    for (int pass = 0; pass < FrontendShared::GPU_TIMED_PASS_COUNT; pass++) {
        if ((recorded_passes & (1U << static_cast<uint32_t>(pass))) == 0U) {
            // Pass not recorded in that frame (e.g. clouds/shadows disabled):
            // show it as inactive and drop stale history so a re-enabled pass
            // starts a fresh average.
            guiRendererSharedVars.gpuTimings.pass_ms[pass] = -1.0F;
            gpu_pass_averages[static_cast<size_t>(pass)].reset();
            continue;
        }

        const size_t start_query = static_cast<size_t>(pass) * GPU_TIMING_QUERIES_PER_PASS;
        const uint64_t start_value = query_data[start_query * 2U];
        const uint64_t start_available = query_data[(start_query * 2U) + 1U];
        const uint64_t end_value = query_data[(start_query + 1U) * 2U];
        const uint64_t end_available = query_data[((start_query + 1U) * 2U) + 1U];

        // Unavailable results are skipped (last smoothed value stays visible).
        if (start_available == 0U || end_available == 0U) { continue; }

        // Modular subtraction masked to timestampValidBits handles counter
        // wraparound on queue families with fewer than 64 valid bits.
        const uint64_t delta_ticks = (end_value - start_value) & gpu_timestamp_mask;
        const auto pass_ms = static_cast<float>(
          static_cast<double>(delta_ticks) * static_cast<double>(gpu_timestamp_period) / NANOSECONDS_PER_MILLISECOND);
        guiRendererSharedVars.gpuTimings.pass_ms[pass] = gpu_pass_averages[static_cast<size_t>(pass)].add(pass_ms);
    }
}

void Kataglyphis::VulkanRenderer::cleanUpUBOs()
{
    // Buffers are persistently mapped by VMA; unmapping happens on destruction.
    for (size_t i = 0; i < globalUBOBuffer.size(); i++) { globalUBOBuffer[i].cleanUp(); }
    for (size_t i = 0; i < sceneUBOBuffer.size(); i++) { sceneUBOBuffer[i].cleanUp(); }
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
    cleanUpFrameCapture();
    // Release the buffer manager's reusable staging buffer while the VMA
    // allocator (torn down in device->cleanUp() below) is still alive.
    vulkanBufferManager.cleanUp();

    destroyGpuTimingResources();
    cleanUpSync();
    cleanUpUBOs();
    cleanUpCommandPools();
    cleanUpDescriptorResources();
    raytracingDescriptors.cleanUp();

    vulkanSwapChain.cleanUp();
    // The device tears down its VMA allocator (after all buffers/images above,
    // before the logical device).
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

void Kataglyphis::VulkanRenderer::create_post_descriptor_resources()
{
    postDescriptors.addBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
      .addBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

    if (!postDescriptors.create(device, vulkanSwapChain.getNumberSwapChainImages())) {
        spdlog::error("Failed to create post descriptor resources!");
    }
}

void Kataglyphis::VulkanRenderer::updatePostDescriptorSets()
{
    if (postDescriptors.sets().size() < vulkanSwapChain.getNumberSwapChainImages()) {
        spdlog::error("Post descriptor sets are not available; skipping update.");
        return;
    }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(i) : deferredRasterizer.getOffscreenTexture(i);
        postDescriptors.writeImage(
          i, 0, renderResult.getImageView(), vk::ImageLayout::eShaderReadOnlyOptimal, postStage.getOffscreenSampler());
        postDescriptors.writeImage(i,
          1,
          clouds.getCloudOutputTexture()->getImageView(),
          vk::ImageLayout::eGeneral,
          clouds.getCloudOutputTexture()->getSampler());
    }
}

void Kataglyphis::VulkanRenderer::createRaytracingDescriptorResources()
{
    const vk::ShaderStageFlags raytracing_stages = vk::ShaderStageFlagBits::eRaygenKHR
                                                   | vk::ShaderStageFlagBits::eClosestHitKHR
                                                   | vk::ShaderStageFlagBits::eCompute;

    raytracingDescriptors.addBinding(TLAS_BINDING, vk::DescriptorType::eAccelerationStructureKHR, 1, raytracing_stages)
      .addBinding(OUT_IMAGE_BINDING, vk::DescriptorType::eStorageImage, 1, raytracing_stages);

    if (!raytracingDescriptors.create(device, vulkanSwapChain.getNumberSwapChainImages())) {
        spdlog::error("Failed to create raytracing descriptor resources!");
    }
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

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        if (i >= sharedRenderDescriptors.sets().size()) { break; }

        sharedRenderDescriptors.writeBuffer(
          i, OBJECT_DESCRIPTION_BINDING, objectDescriptionBuffer.getBuffer(), VK_WHOLE_SIZE);
    }
}

void Kataglyphis::VulkanRenderer::updateRaytracingDescriptorSets()
{
    vk::AccelerationStructureKHR &vulkanTLAS = asManager.getTLAS();
    if (!vulkanTLAS) {
        return;
    }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        Texture &renderResult = guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward ? rasterizer.getOffscreenTexture(i) : deferredRasterizer.getOffscreenTexture(i);

        raytracingDescriptors.writeAccelerationStructure(i, TLAS_BINDING, vulkanTLAS);
        raytracingDescriptors.writeImage(i, OUT_IMAGE_BINDING, renderResult.getImageView(), vk::ImageLayout::eGeneral);
    }
}

void Kataglyphis::VulkanRenderer::createSharedRenderDescriptorResources()
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

    sharedRenderDescriptors.addBinding(globalUBO_BINDING, vk::DescriptorType::eUniformBuffer, 1, global_ubo_stages)
      .addBinding(sceneUBO_BINDING, vk::DescriptorType::eUniformBuffer, 1, scene_ubo_stages)
      .addBinding(OBJECT_DESCRIPTION_BINDING, vk::DescriptorType::eStorageBuffer, 1, object_description_stages)
      .addBinding(
        TEXTURES_BINDING, vk::DescriptorType::eSampledImage, static_cast<uint32_t>(MAX_TEXTURE_COUNT), textures_stages)
      .addBinding(
        SAMPLER_BINDING, vk::DescriptorType::eSampler, static_cast<uint32_t>(MAX_TEXTURE_COUNT), sampler_stages)
      // Cascaded shadow map array, consumed by the forward lighting shader.
      .addBinding(SHADOW_MAP_BINDING, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
      // Historical pool sizing kept as-is (generously overallocated).
      .setPoolSize(
        vk::DescriptorType::eStorageBuffer, static_cast<uint32_t>(sizeof(ObjectDescription) * Kataglyphis::MAX_OBJECTS));

    if (!sharedRenderDescriptors.create(device, vulkanSwapChain.getNumberSwapChainImages())) {
        spdlog::error("Failed to create shared render descriptor resources!");
        return;
    }

    // Initial per-image UBO writes (the remaining bindings are written by the
    // dedicated update methods once their resources exist).
    updateUBODescriptorSets();
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
        
        // Host-visible UBOs are persistently mapped by VMA.
        globalUBOMapped[i] = globalUBOBuffer[i].getMappedData();

        sceneUBOBuffer[i].create(device,
          sizeof(VulkanRendererInternals::SceneUBO),
          vk::BufferUsageFlagBits::eUniformBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        sceneUBOMapped[i] = sceneUBOBuffer[i].getMappedData();
        
        // Initial upload
        update_uniform_buffers(static_cast<uint32_t>(i));
    }
}

void Kataglyphis::VulkanRenderer::updateTexturesInSharedRenderDescriptorSet()
{
    if (sharedRenderDescriptors.sets().empty()) {
        return;
    }

    // Bind the CSM depth array first: it exists independently of scene
    // textures, so it must not sit behind the model early-returns below.
    if (Kataglyphis::Texture *shadow_map_array = dirShadowMap.getShadowMapArray();
        shadow_map_array != nullptr && shadow_map_array->getSampler()) {
        for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
            sharedRenderDescriptors.writeImage(i,
              SHADOW_MAP_BINDING,
              shadow_map_array->getImageView(),
              vk::ImageLayout::eShaderReadOnlyOptimal,
              shadow_map_array->getSampler());
        }
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
        sharedRenderDescriptors.writeImageArray(i, TEXTURES_BINDING, image_info_textures);
        sharedRenderDescriptors.writeImageArray(i, SAMPLER_BINDING, image_info_texture_sampler);
    }
}

void Kataglyphis::VulkanRenderer::create_gbuffer_descriptor_resources()
{
    // 5 input attachments: position, normal, albedo, material, depth.
    for (uint32_t binding = 0; binding < 5; binding++) {
        gbufferDescriptors.addBinding(binding, vk::DescriptorType::eInputAttachment, 1, vk::ShaderStageFlagBits::eFragment);
    }

    if (!gbufferDescriptors.create(device, vulkanSwapChain.getNumberSwapChainImages())) {
        spdlog::error("Failed to create gbuffer descriptor resources!");
    }
}

void Kataglyphis::VulkanRenderer::updateGBufferDescriptorSets()
{
    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        gbufferDescriptors.writeImage(i, 0, deferredRasterizer.getGBufferPosition(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        gbufferDescriptors.writeImage(i, 1, deferredRasterizer.getGBufferNormal(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        gbufferDescriptors.writeImage(i, 2, deferredRasterizer.getGBufferAlbedo(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        gbufferDescriptors.writeImage(i, 3, deferredRasterizer.getGBufferMaterial(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        gbufferDescriptors.writeImage(i, 4, deferredRasterizer.getDepthBufferImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
    }
}
