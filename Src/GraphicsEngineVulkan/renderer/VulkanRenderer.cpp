module;
#include <optional>
#include "common/Utilities.hpp"
#include "common/host_device_shared_vars.hpp"
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
import kataglyphis.vulkan.frustum;
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
    gpuTiming.create(*device, vulkanSwapChain.getNumberSwapChainImages(), gui->getGuiRendererSharedVars());
    create_uniform_buffers();
    create_command_buffers();

    createSynchronization();

    initDescriptorResources();

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_rasterizer = { sharedRenderDescriptors.getLayout() };
    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_deferred = { sharedRenderDescriptors.getLayout(), gbufferDescriptors.getLayout() };

    rasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_rasterizer, graphics_command_pool);
    deferredRasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_deferred, graphics_command_pool);

    clouds.init(device, graphics_command_pool, sharedRenderDescriptors.getLayout(), vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);
    const auto initial_cascade_count = clampCascadeCount(
      static_cast<uint32_t>(MAX_CASCADES), static_cast<uint32_t>(MAX_CASCADES), device->getMaxMultiviewViewCount());
    if (initial_cascade_count < static_cast<uint32_t>(MAX_CASCADES)) {
        spdlog::warn("Device maxMultiviewViewCount ({}) is below MAX_CASCADES ({}); clamping startup cascade count to {}.",
          device->getMaxMultiviewViewCount(), MAX_CASCADES, initial_cascade_count);
    }
    dirShadowMap.init(device, 2048, 2048, initial_cascade_count, sharedRenderDescriptors.getLayout());
    dirShadowMap.createGraphicsPipeline();

    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_post = { postDescriptors.getLayout() };
    postStage.init(device, &vulkanSwapChain, descriptor_set_layouts_post);

    if (device->supportsHardwareAcceleratedRRT()) {
        createRaytracingDescriptorResources();

        std::vector<vk::DescriptorSetLayout> const layouts = { sharedRenderDescriptors.getLayout(),
            raytracingDescriptors.getLayout() };
        raytracingStage.init(device, layouts, &vulkanSwapChain);
        pathTracing.init(device, layouts);
        createPathTracingAccumulationResources();
    }

    updateUniforms(scene, camera, window, gui->getGuiSceneSharedVars());
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

    // Start the parse and carry on initialising. Everything that depends on
    // scene CONTENTS - acceleration structures, the object description buffer,
    // the descriptor sets that reference them - moves into
    // finishModelLoad(), which runs on the frame the model arrives.
    scene->beginModelLoadAsync();

    // Descriptors still need valid contents before the first frame, or the
    // renderer samples never-written bindings while the model loads.
    create_object_description_buffer();
    updateAllDescriptorSets();

    gui->initializeVulkanContext(device,
      instance.getVulkanInstance(),
      postStage.getRenderPass(),
      vulkanSwapChain.getNumberSwapChainImages());
    gui->setUserSelectionForRRT(device->supportsHardwareAcceleratedRRT());
}

void Kataglyphis::VulkanRenderer::updateUniforms(Scene *scene_data,
  Camera *camera_data,
  [[maybe_unused]] Kataglyphis::Frontend::Window *window_data,
  const GUISceneSharedVars &guiSceneSharedVars)
{
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
        glm::vec3(sceneUBO.dirLight.direction),
        guiSceneSharedVars.shadow_distance,
        guiSceneSharedVars.cascade_split_lambda);

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

void Kataglyphis::VulkanRenderer::finishModelLoad()
{
    // Rebuild everything that reads scene contents. Skipping any of these
    // leaves the model present but invisible, or sampled through descriptors
    // that still point at the empty scene.
    if (device->supportsHardwareAcceleratedRRT()) {
        asManager.createASForScene(device, graphics_command_pool, scene);
    }
    rebuildObjectDescriptions();
    updateAllDescriptorSets();
}

void Kataglyphis::VulkanRenderer::updateStateDueToUserInput(GUISceneSharedVars &guiSceneSharedVars)
{
    // Poll the background parse before anything else this frame: the rest of
    // the frame should see the model on the same frame it becomes available.
    if (scene->pollModelLoad(device, graphics_command_pool)) { finishModelLoad(); }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    handleShaderHotReloadRequest(guiRendererSharedVars);
    handleRasterizationModeChange(guiRendererSharedVars);

    handleShadowResolutionChange(guiSceneSharedVars);
    handleModelTransformChange(guiSceneSharedVars);
    handleModelReloadRequest(guiSceneSharedVars);
}

void Kataglyphis::VulkanRenderer::handleShaderHotReloadRequest(
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &guiRendererSharedVars)
{
    if (guiRendererSharedVars.shader_hot_reload_triggered) {
        shaderHotReload();
        guiRendererSharedVars.shader_hot_reload_triggered = false;
    }
}

void Kataglyphis::VulkanRenderer::handleRasterizationModeChange(
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &guiRendererSharedVars)
{
    // Rebind the mode-dependent input descriptors when the rasterization mode
    // changes. record_commands branches on the mode per frame, but the post
    // pass's input image was written once at init - so "Deferred" recorded
    // into an offscreen texture nobody sampled and the screen kept showing
    // the (stale) forward image. Found because the deferred parity golden
    // measured IDENTICAL frames even with the deferred lighting shader forced
    // to output pure red. waitIdle is the same trade the shadow-resolution
    // change below already makes: mode switches are rare, driver-visible UI
    // events, and the alternative is per-image rebind bookkeeping.
    if (guiRendererSharedVars.rasterizationMode != lastBoundRasterizationMode) {
        (void)device->getLogicalDevice().waitIdle();
        updateAllDescriptorSets();
        lastBoundRasterizationMode = guiRendererSharedVars.rasterizationMode;
    }
}

void Kataglyphis::VulkanRenderer::handleShadowResolutionChange(
    GUISceneSharedVars &guiSceneSharedVars)
{
    if (guiSceneSharedVars.shadow_resolution_changed) {
        guiSceneSharedVars.shadow_resolution_changed = false;

        (void)device->getLogicalDevice().waitIdle();
        dirShadowMap.cleanUp();
        
        uint32_t shadow_res = 512;
        if (guiSceneSharedVars.shadow_map_res_index == 1) shadow_res = 1024;
        else if (guiSceneSharedVars.shadow_map_res_index == 2) shadow_res = 2048;
        else if (guiSceneSharedVars.shadow_map_res_index == 3) shadow_res = 4096;

        // Clamp to MAX_CASCADES, matching the startup init: the SceneUBO only
        // has MAX_CASCADES cascade matrices and the shader samples that many, so
        // a GUI value above it (the slider allows up to 8) renders extra cascades
        // that are never sampled. Also clamp to the device's queried
        // maxMultiviewViewCount so the shadow multiview render pass never uses a
        // viewMask whose top bit exceeds it (a validation error) - previously this
        // relied on MAX_CASCADES happening to be small enough for every device.
        const auto device_view_limit = device->getMaxMultiviewViewCount();
        const auto cascade_count = clampCascadeCount(
          static_cast<uint32_t>(guiSceneSharedVars.num_shadow_cascades), static_cast<uint32_t>(MAX_CASCADES), device_view_limit);
        if (cascade_count == device_view_limit && device_view_limit < static_cast<uint32_t>(MAX_CASCADES)) {
            spdlog::warn(
              "Device maxMultiviewViewCount ({}) is the binding constraint on cascade count; clamping to {}.",
              device_view_limit, cascade_count);
        }
        dirShadowMap.init(device, shadow_res, shadow_res, cascade_count, sharedRenderDescriptors.getLayout());
        // cleanUp() destroyed the pipeline, descriptor resources and the light
        // matrices buffer; recreate them (same sequence as at startup).
        dirShadowMap.createGraphicsPipeline();

        // We must recreate descriptor sets that depend on the shadow map
        updateTexturesInSharedRenderDescriptorSet();
    }
}

void Kataglyphis::VulkanRenderer::handleModelTransformChange(
    GUISceneSharedVars &guiSceneSharedVars)
{
    if (guiSceneSharedVars.model_transform_changed) {
        guiSceneSharedVars.model_transform_changed = false;

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
            rebuildObjectDescriptions();

            // The traced world must follow the raster world: without this,
            // RT/PT kept tracing the OLD pose after a GUI transform change.
            // BLAS geometry is untouched - only the instance transform moved
            // - so rebuilding the TLAS alone is enough. Must run BEFORE the
            // descriptor update below, which binds the new TLAS handle (and
            // resets the PT accumulation history this change invalidates).
            if (device->supportsHardwareAcceleratedRRT()) {
                asManager.createTLAS(device, graphics_command_pool, scene);
            }

            updateAllDescriptorSets();
        }


    }
}

void Kataglyphis::VulkanRenderer::handleModelReloadRequest(
    GUISceneSharedVars &guiSceneSharedVars)
{
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

            rebuildObjectDescriptions();

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

void Kataglyphis::VulkanRenderer::drawFrame(const GUISceneSharedVars &guiSceneSharedVars)
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

    if (frameSync.frameSyncCount() == 0) {
        spdlog::error("No synchronization frames available; skipping draw frame.");
        end_imgui_frame_if_needed();
        return;
    }

    if (checkChangedFramebufferSize()) {
        if (frameSync.frameSyncCount() > 0 && !frameSync.inFlightFencesEmpty()) {
            recreateSwapChain();
        }
    }

    if (frameSync.currentFrame() >= frameSync.inFlightFenceCount()
        || frameSync.currentFrame() >= frameSync.imageAvailableCount()) {
        spdlog::error(fmt::format("Frame synchronization index out of range: {}", frameSync.currentFrame()));
        end_imgui_frame_if_needed();
        return;
    }

    if (!frameSync.inFlightFence() || !frameSync.imageAvailableSemaphore()) {
        spdlog::error(fmt::format("Synchronization handles are invalid for frame {}.", frameSync.currentFrame()));
        if (window != nullptr && window->get_window() != nullptr) {
            glfwSetWindowShouldClose(window->get_window(), GLFW_TRUE);
        }
        end_imgui_frame_if_needed();
        return;
    }

    vk::Result result = device->getLogicalDevice().waitForFences(
      1, &frameSync.inFlightFence(), VK_TRUE, std::numeric_limits<uint64_t>::max());
    if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to wait for fences!", result);
        return;
    }

    uint32_t image_index = 0;
    std::tie(result, image_index) = device->getLogicalDevice().acquireNextImageKHR(
      vulkanSwapChain.getSwapChain(), std::numeric_limits<uint64_t>::max(), frameSync.imageAvailableSemaphore(),
      nullptr);

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

    if (image_index >= frameSync.imagesInFlightFenceCount() || image_index >= command_buffers.size()) {
        spdlog::error(fmt::format("Swapchain image index out of range: {}", image_index));
        end_imgui_frame_if_needed();
        return;
    }

    if (image_index >= frameSync.renderFinishedCount() || !frameSync.renderFinishedSemaphore(image_index)) {
        spdlog::error(fmt::format("Render-finished semaphore missing for swapchain image {}.", image_index));
        end_imgui_frame_if_needed();
        return;
    }

    if (frameSync.imageInFlightFence(image_index)) {
        result = device->getLogicalDevice().waitForFences(
          1, &frameSync.imageInFlightFence(image_index), VK_TRUE, UINT64_MAX);
        if (result != vk::Result::eSuccess) {
            abort_frame_with_fatal_error("Failed to wait for image in-flight fence!", result);
            return;
        }
    }

    // The fence wait above guarantees the previous commands that used this
    // swapchain image (and its query slice) completed, so the readback below
    // never has to wait on the GPU.
    gpuTiming.readTimings(*device, image_index, gui->getGuiRendererSharedVars());

    frameSync.imageInFlightFence(image_index) = frameSync.inFlightFence();

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

    if (!record_commands(image_index, guiSceneSharedVars)) {
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
    submit_info.pWaitSemaphores = &frameSync.imageAvailableSemaphore();

    vk::PipelineStageFlags const wait_stages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    submit_info.pWaitDstStageMask = &wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers[image_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &frameSync.renderFinishedSemaphore(image_index);

    result = device->getLogicalDevice().resetFences(1, &frameSync.inFlightFence());
    if (result != vk::Result::eSuccess) {
        abort_frame_with_fatal_error("Failed to reset fences!", result);
        return;
    }

    result = device->getGraphicsQueue().submit(1, &submit_info, frameSync.inFlightFence());
    if (result != vk::Result::eSuccess) {
        spdlog::error(
          fmt::format("Queue submit context: frame={}, imageIndex={}, renderMode={}, supportsRRT={}, cmdBufferIndex={}",
            frameSync.currentFrame(),
            image_index,
            render_mode,
            raytracing_available,
            image_index));
        abort_frame_with_fatal_error("Failed to submit command buffer to queue!", result);
        return;
    }

    // A capture recorded into this command buffer completes when this frame's
    // in-flight fence signals; takeCapturedFrame() waits on exactly that.
    frameCapture.bindSubmitFence(frameSync.inFlightFence());

    vk::PresentInfoKHR present_info{};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &frameSync.renderFinishedSemaphore(image_index);
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

    frameSync.advanceFrame();
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

void Kataglyphis::VulkanRenderer::reprovisionPerImageResources()
{
    cleanUpUBOs();
    create_uniform_buffers();

    cleanUpDescriptorResources();
    initDescriptorResources();

    if (device->supportsHardwareAcceleratedRRT()) {
        raytracingDescriptors.cleanUp();
        createRaytracingDescriptorResources();
    }
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
    frameCapture.invalidateFence();

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
    gpuTiming.create(*device, vulkanSwapChain.getNumberSwapChainImages(), gui->getGuiRendererSharedVars());

    uint32_t newImageCount = vulkanSwapChain.getNumberSwapChainImages();

    // Recreate depth buffers and framebuffers with new swapchain
    postStage.recreateFrameResources();
    rasterizer.recreateFrameResources(graphics_command_pool);
    deferredRasterizer.recreateFrameResources(graphics_command_pool);
    clouds.recreateFrameResources(graphics_command_pool, vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);

    // The accumulation history is swapchain-extent-sized; recreate it (which
    // also resets the frame counter - the old history is meaningless at the
    // new resolution). updateAllDescriptorSets() below rewrites its binding.
    if (device->supportsHardwareAcceleratedRRT()) { createPathTracingAccumulationResources(); }

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
        reprovisionPerImageResources();
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

std::optional<uint32_t> Kataglyphis::VulkanRenderer::addModel(const std::string &modelPath,
  const glm::mat4 &modelMatrix)
{
    if (!scene || !device) { return std::nullopt; }

    // Uploads happen on the graphics queue; make sure nothing is mid-flight
    // reading the descriptor sets this is about to rewrite.
    std::ignore = device->getLogicalDevice().waitIdle();

    const std::optional<uint32_t> index =
      scene->loadAdditionalModel(device, graphics_command_pool, modelPath, modelMatrix);
    if (!index.has_value()) { return std::nullopt; }

    // Release the old buffer BEFORE building the replacement. Skipping this
    // leaks the previous allocation and trips VMA's
    // "some allocations were not freed" assertion when the block is
    // destroyed - which is how the two-model test first failed.
    rebuildObjectDescriptions();

    // The added model is NEW geometry, so its BLAS must be built and the TLAS
    // rebuilt to reference it - otherwise it loads and renders in the raster
    // paths (which iterate the scene directly) but is INVISIBLE to RT/PT, which
    // only see the acceleration structure. Unlike a transform change (TLAS-only,
    // see updateStateDueToUserInput), new geometry needs the BLAS too, so this is
    // the full createASForScene (it clears the old BLAS/TLAS first, so a rebuild
    // is safe). Must run BEFORE updateAllDescriptorSets, which binds the new TLAS.
    if (device->supportsHardwareAcceleratedRRT()) {
        asManager.createASForScene(device, graphics_command_pool, scene);
    }
    updateAllDescriptorSets();
    return index;
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

    Texture &renderResult = activeOffscreenTexture(image_index);

    raytracingDescriptors.writeAccelerationStructure(image_index, TLAS_BINDING, vulkanTLAS);
    raytracingDescriptors.writeImage(image_index, OUT_IMAGE_BINDING, renderResult.getImageView(), vk::ImageLayout::eGeneral);
    raytracingDescriptors.writeImage(
      image_index, ACCUMULATION_IMAGE_BINDING, pathTracingAccumulation.getImageView(), vk::ImageLayout::eGeneral);
}

auto Kataglyphis::VulkanRenderer::activeOffscreenTexture(uint32_t index) -> Texture &
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();
    return guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward
             ? rasterizer.getOffscreenTexture(index)
             : deferredRasterizer.getOffscreenTexture(index);
}

bool Kataglyphis::VulkanRenderer::record_commands(uint32_t image_index, const GUISceneSharedVars &guiSceneSharedVars)
{
    if (image_index >= command_buffers.size() || image_index >= sharedRenderDescriptors.sets().size()
        || image_index >= postDescriptors.sets().size()) {
        spdlog::error(fmt::format("Command recording index out of range: {}", image_index));
        return false;
    }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    vk::CommandBuffer &commandBuffer = command_buffers[image_index];

    namespace FrontendShared = Kataglyphis::VulkanRendererInternals::FrontendShared;
    using FrontendShared::GpuTimedPass;

    // -- per-pass GPU timing: reset this image's query slice (outside any
    // render pass) and bracket every recorded pass with a timestamp pair.
    const bool record_gpu_timings = gpuTiming.isSupported() && gpuTiming.queryPool();
    const uint32_t gpu_timing_base = image_index * gpuTiming.queriesPerImage();
    uint32_t recorded_pass_mask = 0U;

    if (record_gpu_timings) {
        commandBuffer.resetQueryPool(gpuTiming.queryPool(), gpu_timing_base, gpuTiming.queriesPerImage());
    }

    const auto write_pass_timestamp = [&](GpuTimedPass pass, bool start) -> void {
        if (!record_gpu_timings) { return; }
        const uint32_t pass_index = static_cast<uint32_t>(pass);
        const uint32_t query =
          gpu_timing_base + pass_index * Kataglyphis::GpuTimingSubsystem::QUERIES_PER_PASS + (start ? 0U : 1U);
        commandBuffer.writeTimestamp(
          start ? vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eBottomOfPipe,
          gpuTiming.queryPool(),
          query);
        if (!start) { recorded_pass_mask |= (1U << pass_index); }
    };

    std::vector<vk::DescriptorSet> rasterizer_descriptor_sets = { sharedRenderDescriptors.sets()[image_index] };

    if (guiSceneSharedVars.clouds_enabled) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "clouds", { 0.80F, 0.85F, 0.95F, 1.0F });
        write_pass_timestamp(GpuTimedPass::Clouds, true);
        clouds.recordComputeCommands(commandBuffer, rasterizer_descriptor_sets);
        write_pass_timestamp(GpuTimedPass::Clouds, false);
    }

    if (guiSceneSharedVars.shadows_enabled) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "shadow_cascades", { 0.55F, 0.35F, 0.10F, 1.0F });
        write_pass_timestamp(GpuTimedPass::ShadowCascades, true);
        dirShadowMap.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets);
        write_pass_timestamp(GpuTimedPass::ShadowCascades, false);
    }

    write_pass_timestamp(GpuTimedPass::Main, true);

    // One extraction per frame, shared by both raster paths. Built from the
    // SAME matrices the vertex shaders use (globalUBO), so what is culled and
    // what is drawn cannot disagree.
    //
    // Note this is deliberately computed AFTER the shadow pass above: shadow
    // casters must not be culled by the camera frustum, because geometry
    // beside or behind the camera still casts into view.
    const std::optional<FrustumPlanes> camera_frustum =
      guiRendererSharedVars.frustum_culling_enabled
        ? std::optional<FrustumPlanes>(extractFrustumPlanes(globalUBO.projection * globalUBO.view))
        : std::nullopt;

    recordRasterPass(commandBuffer, image_index, rasterizer_descriptor_sets, camera_frustum);

    recordRaytracingOrPathTracing(commandBuffer, image_index);

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
    if (frameCapture.isArmed()) {
        frameCapture.record(device, commandBuffer, vulkanSwapChain, image_index, device_lost_detected);
    }

    if (record_gpu_timings) { gpuTiming.setPassRecordedMask(image_index, recorded_pass_mask); }

    return true;
}

void Kataglyphis::VulkanRenderer::recordRasterPass(vk::CommandBuffer &commandBuffer,
  uint32_t image_index,
  const std::vector<vk::DescriptorSet> &rasterizer_descriptor_sets,
  const std::optional<FrustumPlanes> &camera_frustum)
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    if (guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "forward", { 0.20F, 0.60F, 1.00F, 1.0F });
        rasterizer.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets, camera_frustum);
    } else {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "deferred", { 0.20F, 0.40F, 0.80F, 1.0F });
        std::vector<vk::DescriptorSet> deferred_sets = { sharedRenderDescriptors.sets()[image_index], gbufferDescriptors.sets()[image_index] };
        deferredRasterizer.recordCommands(commandBuffer, image_index, scene, deferred_sets, camera_frustum);
    }

    // Publish whichever path actually recorded this frame. Reading both and
    // summing would double-count the shared scene; reading the inactive one
    // would report last frame's numbers from before the mode switch.
    const bool forward_active =
      guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward;
    // The local binding above is const; the stats are renderer output, so
    // take the mutable reference the same way the GPU-timing code does.
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &mutable_gui_vars =
      gui->getGuiRendererSharedVars();
    mutable_gui_vars.visibility.meshes_drawn =
      forward_active ? rasterizer.getMeshesDrawn() : deferredRasterizer.getMeshesDrawn();
    mutable_gui_vars.visibility.meshes_total =
      forward_active ? rasterizer.getMeshesConsidered() : deferredRasterizer.getMeshesConsidered();
}

void Kataglyphis::VulkanRenderer::recordRaytracingOrPathTracing(vk::CommandBuffer &commandBuffer, uint32_t image_index)
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    // The TLAS guard covers the async model-load window: with RT/PT enabled
    // before the scene arrives, the record path used to dispatch against
    // descriptor sets that were never written (TLAS, output, accumulation) -
    // 20 validation errors in the pre-load frames of the accumulation golden.
    if (device->supportsHardwareAcceleratedRRT() && image_index < raytracingDescriptors.sets().size()
        && asManager.getTLAS()) {
        std::vector<vk::DescriptorSet> raytracing_descriptor_sets = { sharedRenderDescriptors.sets()[image_index],
            raytracingDescriptors.sets()[image_index] };

        if (guiRendererSharedVars.raytracing) {
            Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "raytracing", { 0.85F, 0.25F, 0.55F, 1.0F });
            Texture &renderResult = activeOffscreenTexture(image_index);
            raytracingStage.recordCommands(
              commandBuffer, renderResult.getVulkanImage(), &vulkanSwapChain, raytracing_descriptor_sets);
        } else if (guiRendererSharedVars.pathTracing) {
            Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "pathtracing", { 0.60F, 0.25F, 0.85F, 1.0F });
            Texture &renderResult = activeOffscreenTexture(image_index);

            // A camera move or a quality change invalidates the accumulated
            // history; restart the running mean from this frame.
            glm::mat4 const current_view = camera->calculate_viewmatrix();
            if (current_view != pathTracingLastView
                || guiRendererSharedVars.pathTracingSamplesPerPixel != pathTracingLastSamples
                || guiRendererSharedVars.pathTracingMaxBounces != pathTracingLastBounces) {
                pathTracingAccumulatedFrames = 0;
                pathTracingLastView = current_view;
                pathTracingLastSamples = guiRendererSharedVars.pathTracingSamplesPerPixel;
                pathTracingLastBounces = guiRendererSharedVars.pathTracingMaxBounces;
            }

            pathTracing.recordCommands(commandBuffer,
              image_index,
              renderResult.getVulkanImage(),
              pathTracingAccumulation.getVulkanImage(),
              &vulkanSwapChain,
              raytracing_descriptor_sets,
              pathTracingAccumulatedFrames,
              static_cast<uint32_t>(std::max(guiRendererSharedVars.pathTracingSamplesPerPixel, 1)),
              static_cast<uint32_t>(std::max(guiRendererSharedVars.pathTracingMaxBounces, 1)));
            ++pathTracingAccumulatedFrames;
        }
    }
}

auto Kataglyphis::VulkanRenderer::supportsFrameCapture() const -> bool
{
    return device != nullptr && frameCapture.supportsCapture(vulkanSwapChain, device_lost_detected);
}

void Kataglyphis::VulkanRenderer::requestFrameCapture()
{
    if (!supportsFrameCapture()) {
        spdlog::warn("Frame capture requested but the surface does not support eTransferSrc; ignoring.");
        return;
    }

    frameCapture.request();
}

auto Kataglyphis::VulkanRenderer::takeCapturedFrame(uint32_t &outWidth, uint32_t &outHeight) -> std::vector<uint8_t>
{
    return frameCapture.take(device, device_lost_detected, outWidth, outHeight);
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

    // Inside the !device guard so the export runs exactly once even though
    // cleanUp is reached twice (explicitly, then again from the destructor).
    // The final in-flight frames' queries are never read back - readTimings
    // only runs on the NEXT use of a swapchain image - so the average covers
    // every frame except the last swapchain-image-count of them, which is fine
    // for a mean over a whole run.
    gpuTiming.writeJsonIfRequested();

    if (device->supportsHardwareAcceleratedRRT()) {
        pathTracingAccumulation.cleanUp();
        pathTracing.cleanUp();
        raytracingStage.cleanUp();
        asManager.cleanUp();
    }

    rasterizer.cleanUp();
    deferredRasterizer.cleanUp();
    skyBox.cleanUp();
    clouds.cleanUp();
    dirShadowMap.cleanUp();
    postStage.cleanUp();

    objectDescriptionBuffer.cleanUp();
    frameCapture.cleanUp();
    // Release the buffer manager's reusable staging buffer while the VMA
    // allocator (torn down in device->cleanUp() below) is still alive.
    vulkanBufferManager.cleanUp();

    gpuTiming.destroy(*device);
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

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        Texture &renderResult = activeOffscreenTexture(i);
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
      .addBinding(OUT_IMAGE_BINDING, vk::DescriptorType::eStorageImage, 1, raytracing_stages)
      .addBinding(ACCUMULATION_IMAGE_BINDING, vk::DescriptorType::eStorageImage, 1, raytracing_stages);

    if (!raytracingDescriptors.create(device, vulkanSwapChain.getNumberSwapChainImages())) {
        spdlog::error("Failed to create raytracing descriptor resources!");
    }
}

void Kataglyphis::VulkanRenderer::createPathTracingAccumulationResources()
{
    // Also the resize path: drop any previous image (and with it the history).
    pathTracingAccumulation.cleanUp();

    vk::Extent2D const extent = vulkanSwapChain.getSwapChainExtent();
    pathTracingAccumulation.createImage(device,
      extent.width,
      extent.height,
      1,
      vk::Format::eR32G32B32A32Sfloat,
      vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eStorage,
      vk::MemoryPropertyFlagBits::eDeviceLocal);
    pathTracingAccumulation.createImageView(
      device, vk::Format::eR32G32B32A32Sfloat, vk::ImageAspectFlagBits::eColor, 1);

    // Storage images live in eGeneral for their whole lifetime (same pattern
    // as the clouds output texture).
    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(
      device->getLogicalDevice(), graphics_command_pool);
    pathTracingAccumulation.getVulkanImage().transitionImageLayout(
      commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, vk::ImageAspectFlagBits::eColor);
    Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device->getLogicalDevice(), graphics_command_pool, device->getGraphicsQueue(), commandBuffer);

    pathTracingAccumulatedFrames = 0;
}

void Kataglyphis::VulkanRenderer::cleanUpSync() { frameSync.cleanUp(device->getLogicalDevice()); }

void Kataglyphis::VulkanRenderer::rebuildObjectDescriptions()
{
    objectDescriptionBuffer.cleanUp();
    create_object_description_buffer();
}

void Kataglyphis::VulkanRenderer::create_object_description_buffer()
{
    std::vector<ObjectDescription> objectDescriptions = scene->getObjectDescriptions();

    // Fill in each model's slot into the flattened global texture array
    // (descriptions are pushed one per model, in model order). The shaders
    // add this to the model-LOCAL material textureIDs.
    uint64_t texture_offset = 0;
    for (size_t i = 0; i < objectDescriptions.size() && i < scene->getModelCount(); ++i) {
        objectDescriptions[i].texture_offset = texture_offset;
        texture_offset += scene->getTextureCount(static_cast<uint32_t>(i));
    }

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

    // This runs when the traced world changes (model load/reload rebuilt the
    // AS, mode switch). Any accumulated history predates that world: without
    // this reset, frames traced against the HALF-LOADED scene stay blended
    // into the running mean until the camera happens to move (observed as the
    // accumulation golden "converging" with per-frame sampling disabled - the
    // mean was healing from startup frames, not averaging samples).
    pathTracingAccumulatedFrames = 0;

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        Texture &renderResult = activeOffscreenTexture(i);

        raytracingDescriptors.writeAccelerationStructure(i, TLAS_BINDING, vulkanTLAS);
        raytracingDescriptors.writeImage(i, OUT_IMAGE_BINDING, renderResult.getImageView(), vk::ImageLayout::eGeneral);
        raytracingDescriptors.writeImage(
          i, ACCUMULATION_IMAGE_BINDING, pathTracingAccumulation.getImageView(), vk::ImageLayout::eGeneral);
    }
}

void Kataglyphis::VulkanRenderer::createSharedRenderDescriptorResources()
{
    const bool raytracing_available = device->supportsHardwareAcceleratedRRT();

    // eFragment: the deferred lighting pass reads inv_view/inv_projection to
    // reconstruct world position from depth.
    vk::ShaderStageFlags global_ubo_stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
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
}

void Kataglyphis::VulkanRenderer::cleanUpCommandPools()
{
    if (graphics_command_pool) {
        device->getLogicalDevice().destroyCommandPool(graphics_command_pool);
        graphics_command_pool = nullptr;
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
    frameSync.create(device->getLogicalDevice(), vulkanSwapChain.getNumberSwapChainImages());
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

    // Flatten EVERY model's textures into the global array, in model order -
    // the same order create_object_description_buffer assigns each model's
    // texture_offset. This function used to bind model 0's textures only, so
    // any added model shaded with the FIRST model's images (its local
    // textureIDs collided with model 0's slots).
    std::vector<vk::DescriptorImageInfo> image_info_textures;
    std::vector<vk::DescriptorImageInfo> image_info_texture_sampler;
    image_info_textures.reserve(MAX_TEXTURE_COUNT);
    image_info_texture_sampler.reserve(MAX_TEXTURE_COUNT);

    for (uint32_t model = 0; model < scene->getModelCount(); ++model) {
        std::vector<Texture> &modelTextures = scene->getTextures(model);
        std::vector<vk::Sampler> &modelTextureSampler = scene->getTextureSampler(model);
        const uint32_t model_texture_count = scene->getTextureCount(model);
        for (uint32_t t = 0; t < model_texture_count; ++t) {
            if (image_info_textures.size() >= MAX_TEXTURE_COUNT) {
                spdlog::warn(
                  "Texture slots exhausted: {} textures across {} models exceed MAX_TEXTURE_COUNT={} - "
                  "models past the cap will sample the wrong slots.",
                  image_info_textures.size() + (model_texture_count - t),
                  scene->getModelCount(),
                  MAX_TEXTURE_COUNT);
                break;
            }
            vk::DescriptorImageInfo texture_info{};
            texture_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            texture_info.imageView = modelTextures[t].getImageView();
            image_info_textures.push_back(texture_info);

            vk::DescriptorImageInfo sampler_info{};
            sampler_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            sampler_info.sampler = modelTextureSampler[t];
            image_info_texture_sampler.push_back(sampler_info);
        }
    }

    if (image_info_textures.empty()) { return; }

    // Pad the fixed-size binding arrays with slot 0 (every slot must hold a
    // valid descriptor).
    while (image_info_textures.size() < MAX_TEXTURE_COUNT) {
        image_info_textures.push_back(image_info_textures.front());
        image_info_texture_sampler.push_back(image_info_texture_sampler.front());
    }

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        sharedRenderDescriptors.writeImageArray(i, TEXTURES_BINDING, image_info_textures);
        sharedRenderDescriptors.writeImageArray(i, SAMPLER_BINDING, image_info_texture_sampler);
    }
}

void Kataglyphis::VulkanRenderer::create_gbuffer_descriptor_resources()
{
    // 4 input attachments: normal, albedo, material, depth. World position
    // is reconstructed from depth in the lighting pass.
    for (uint32_t binding = 0; binding < 4; binding++) {
        gbufferDescriptors.addBinding(binding, vk::DescriptorType::eInputAttachment, 1, vk::ShaderStageFlagBits::eFragment);
    }

    if (!gbufferDescriptors.create(device, vulkanSwapChain.getNumberSwapChainImages())) {
        spdlog::error("Failed to create gbuffer descriptor resources!");
    }
}

void Kataglyphis::VulkanRenderer::updateGBufferDescriptorSets()
{
    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        gbufferDescriptors.writeImage(i, 0, deferredRasterizer.getGBufferNormal(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        gbufferDescriptors.writeImage(i, 1, deferredRasterizer.getGBufferAlbedo(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        gbufferDescriptors.writeImage(i, 2, deferredRasterizer.getGBufferMaterial(i), vk::ImageLayout::eShaderReadOnlyOptimal);
        gbufferDescriptors.writeImage(i, 3, deferredRasterizer.getDepthBufferImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
    }
}
