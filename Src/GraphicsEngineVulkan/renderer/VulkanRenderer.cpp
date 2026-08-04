module;
#include <optional>
#include "common/GuiModelTransform.hpp"
#include "common/SceneUboMarshal.hpp"
#include "common/Utilities.hpp"
#include "common/host_device_shared_vars.hpp"
#include "renderer/PathTracingHistory.hpp"
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
#include <span>
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
import kataglyphis.vulkan.color_attachment;

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

    std::array<vk::DescriptorSetLayout, 1> const descriptor_set_layouts_rasterizer = { sharedRenderDescriptors.getLayout() };
    std::array<vk::DescriptorSetLayout, 2> const descriptor_set_layouts_deferred = { sharedRenderDescriptors.getLayout(), gbufferDescriptors.getLayout() };

    rasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_rasterizer, graphics_command_pool);
    deferredRasterizer.init(device, &vulkanSwapChain, descriptor_set_layouts_deferred);

    clouds.init(device, graphics_command_pool, sharedRenderDescriptors.getLayout(), vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);
    const auto initial_cascade_count = clampCascadeCount(
      static_cast<uint32_t>(MAX_CASCADES), static_cast<uint32_t>(MAX_CASCADES), device->getMaxMultiviewViewCount());
    if (initial_cascade_count < static_cast<uint32_t>(MAX_CASCADES)) {
        spdlog::warn("Device maxMultiviewViewCount ({}) is below MAX_CASCADES ({}); clamping startup cascade count to {}.",
          device->getMaxMultiviewViewCount(), MAX_CASCADES, initial_cascade_count);
    }
    // The GUI is the single source of truth for the startup shadow-map
    // resolution, so it can no longer disagree with what the combo shows.
    constexpr GUISceneSharedVars kGuiDefaults{};
    const uint32_t initial_shadow_res = shadowResolutionForIndex(kGuiDefaults.shadow_map_res_index);
    dirShadowMap.init(device, initial_shadow_res, initial_shadow_res, initial_cascade_count, sharedRenderDescriptors.getLayout(), vulkanSwapChain.getNumberSwapChainImages(), graphics_command_pool);
    dirShadowMap.createGraphicsPipeline();

    std::array<vk::DescriptorSetLayout, 1> const descriptor_set_layouts_post = { postDescriptors.getLayout() };
    postStage.init(device, &vulkanSwapChain, descriptor_set_layouts_post);

    if (device->supportsHardwareAcceleratedRRT()) {
        createRaytracingDescriptorResources();

        std::array<vk::DescriptorSetLayout, 2> const layouts = { sharedRenderDescriptors.getLayout(),
            raytracingDescriptors.getLayout() };
        raytracingStage.init(device, layouts, &vulkanSwapChain);
        pathTracing.init(device, layouts);
        createPathTracingAccumulationResources();
    }

    updateUniforms(scene, camera, gui->getGuiSceneSharedVars());
    updateAllDescriptorSets();

    // Seed every swapchain image's light-matrix buffer with the just-computed
    // cascades, matching create_uniform_buffers()'s per-image initial upload
    // for globalUBO/sceneUBO above: without this only the image the first
    // drawFrame() happens to acquire gets real matrices, and the rest keep
    // whatever createDescriptorSetAndPipeline() uploaded before dirShadowMap
    // had ever computed a cascade (i.e. default-constructed matrices) until
    // drawFrame eventually cycles back to them.
    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) { dirShadowMap.uploadLightMatrices(i); }

    skyBox.init(device, graphics_command_pool);
    skyBox.createRenderPass(vulkanSwapChain.getSwapChainFormat(), postStage.getDepthFormat());
    skyBox.createGraphicsPipeline(sharedRenderDescriptors.getLayout());
    skyBox.createFramebuffers(swapchainImageViews(), postStage.getDepthBufferImageView(),
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
  const GUISceneSharedVars &guiSceneSharedVars)
{
    const vk::Extent2D extent = vulkanSwapChain.getSwapChainExtent();
    float const aspect_ratio = aspectRatioOf(extent.width, extent.height);

    globalUBO.view = camera_data->calculate_viewmatrix();
    globalUBO.projection = makeVulkanProjection(camera_data->get_fov(),
      aspect_ratio,
      camera_data->get_near_plane(),
      camera_data->get_far_plane());

    fillSceneUboCamera(sceneUBO, camera_data->get_camera_position(), camera_data->get_camera_direction());

    fillSceneUboDirectionalLight(sceneUBO,
      glm::vec3(guiSceneSharedVars.directional_light_direction[0],
        guiSceneSharedVars.directional_light_direction[1],
        guiSceneSharedVars.directional_light_direction[2]),
      glm::vec3(guiSceneSharedVars.directional_light_color[0],
        guiSceneSharedVars.directional_light_color[1],
        guiSceneSharedVars.directional_light_color[2]),
      guiSceneSharedVars.directional_light_radiance);

    // Populate GUI state into SceneUBO
    sceneUBO.pcfRadius = clampPcfRadius(guiSceneSharedVars.pcf_radius);
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
    std::array<float, MAX_CASCADES> cascadeSplitDepths{};
    std::array<glm::mat4, MAX_CASCADES> cascadeViewProjMatrices{};
    for (size_t i = 0; i < active_cascades; ++i) {
        cascadeSplitDepths[i] = cascadeData[i].splitDepth;
        cascadeViewProjMatrices[i] = cascadeData[i].viewProjMatrix;
    }
    fillSceneUboCascades(sceneUBO,
      std::span<const float>(cascadeSplitDepths).first(active_cascades),
      std::span<const glm::mat4>(cascadeViewProjMatrices).first(active_cascades),
      guiSceneSharedVars.shadows_enabled);

    fillSceneUboClouds(sceneUBO,
      glm::vec3(guiSceneSharedVars.cloud_mesh_scale[0],
        guiSceneSharedVars.cloud_mesh_scale[1],
        guiSceneSharedVars.cloud_mesh_scale[2]),
      guiSceneSharedVars.cloud_density_multiplier,
      glm::vec3(guiSceneSharedVars.cloud_mesh_offset[0],
        guiSceneSharedVars.cloud_mesh_offset[1],
        guiSceneSharedVars.cloud_mesh_offset[2]),
      guiSceneSharedVars.cloud_coverage_threshold,
      guiSceneSharedVars.cloud_num_march_steps,
      guiSceneSharedVars.cloud_num_march_steps_to_light,
      guiSceneSharedVars.cloud_pillowness,
      guiSceneSharedVars.cloud_cirrus_effect,
      guiSceneSharedVars.cloud_powder_effect);
}

auto Kataglyphis::VulkanRenderer::supportsHardwareRaytracing() const -> bool
{
    return device && device->supportsHardwareAcceleratedRRT();
}

void Kataglyphis::VulkanRenderer::finishModelLoad() { refreshAfterSceneChange(true); }

void Kataglyphis::VulkanRenderer::refreshAfterSceneChange(bool rebuildBottomLevel)
{
    // Rebuild everything that reads scene contents. Skipping any of these
    // leaves the model present but invisible, or sampled/traced through
    // descriptors and acceleration structures that still point at stale data.
    rebuildObjectDescriptions();

    // Must run BEFORE updateAllDescriptorSets, which binds the (possibly new)
    // TLAS handle and resets the PT accumulation history a rebuild invalidates.
    if (device->supportsHardwareAcceleratedRRT()) {
        if (rebuildBottomLevel) {
            asManager.createASForScene(device, graphics_command_pool, scene);
        } else {
            asManager.createTLAS(device, graphics_command_pool, scene);
        }
    }

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

void Kataglyphis::VulkanRenderer::reinitShadowMapForCurrentSettings()
{
    GUISceneSharedVars &guiSceneSharedVars = gui->getGuiSceneSharedVars();

    dirShadowMap.cleanUp();

    const uint32_t shadow_res = shadowResolutionForIndex(guiSceneSharedVars.shadow_map_res_index);

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
    dirShadowMap.init(device, shadow_res, shadow_res, cascade_count, sharedRenderDescriptors.getLayout(), vulkanSwapChain.getNumberSwapChainImages(), graphics_command_pool);
    // cleanUp() destroyed the pipeline, descriptor resources and the light
    // matrices buffer; recreate them (same sequence as at startup).
    dirShadowMap.createGraphicsPipeline();
}

void Kataglyphis::VulkanRenderer::handleShadowResolutionChange(
    GUISceneSharedVars &guiSceneSharedVars)
{
    if (guiSceneSharedVars.shadow_resolution_changed) {
        guiSceneSharedVars.shadow_resolution_changed = false;

        (void)device->getLogicalDevice().waitIdle();

        reinitShadowMapForCurrentSettings();

        // We must recreate descriptor sets that depend on the shadow map
        updateTexturesInSharedRenderDescriptorSet();
    }
}

void Kataglyphis::VulkanRenderer::handleModelTransformChange(
    GUISceneSharedVars &guiSceneSharedVars)
{
    if (guiSceneSharedVars.model_transform_changed) {
        guiSceneSharedVars.model_transform_changed = false;

        const glm::mat4 modelMatrix = makeGuiModelTransform(
          std::span<const float, 3>(guiSceneSharedVars.model_position),
          std::span<const float, 3>(guiSceneSharedVars.model_rotation));

        // selected_model_index is a *file-list* index into
        // sceneConfig::getAvailableModelPaths() (GUI.cpp:88-94), not a scene
        // model index - the transform always targets scene model 0, which is
        // what reloadModel leaves behind.
        if (guiSceneSharedVars.selected_model_index >= 0) {
            scene->update_model_matrix(modelMatrix, 0);

            // Re-upload object descriptions and refresh the traced world so it
            // follows the raster world. BLAS geometry is untouched - only the
            // instance transform moved - so the TLAS-only rebuild is enough.
            (void)device->getLogicalDevice().waitIdle();
            refreshAfterSceneChange(false);
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

            // The reload swaps in entirely new geometry, so this needs the
            // same full refresh as addModel (BLAS+TLAS rebuild and every
            // descriptor set, not just the shared-texture one) - otherwise
            // RT/PT keep the destroyed TLAS bound and the GBuffer/post/
            // raytracing descriptors stay pointed at the old scene.
            refreshAfterSceneChange(true);
        }
    }
}

void Kataglyphis::VulkanRenderer::finishAllRenderCommands() { std::ignore = device->getLogicalDevice().waitIdle(); }

// Reloads every stage that owns a shaderHotReload implementation:
// rasterizer, deferredRasterizer, postStage, skyBox, dirShadowMap, clouds and
// (when supported) raytracingStage/pathTracing - i.e. all eight subsystems
// that load SPIR-V, per
// BuildIntegrity.EverySpirvLoadingSubsystemImplementsShaderHotReload.
void Kataglyphis::VulkanRenderer::shaderHotReload()
{
    std::ignore = device->getLogicalDevice().waitIdle();

    std::array<vk::DescriptorSetLayout, 1> const descriptor_set_layouts = { sharedRenderDescriptors.getLayout() };
    rasterizer.shaderHotReload(descriptor_set_layouts);

    std::array<vk::DescriptorSetLayout, 2> const descriptor_set_layouts_deferred = { sharedRenderDescriptors.getLayout(),
        gbufferDescriptors.getLayout() };
    deferredRasterizer.shaderHotReload(descriptor_set_layouts_deferred);

    std::array<vk::DescriptorSetLayout, 1> const descriptor_set_layouts_post = { postDescriptors.getLayout() };
    postStage.shaderHotReload(descriptor_set_layouts_post);

    skyBox.shaderHotReload(sharedRenderDescriptors.getLayout());
    dirShadowMap.shaderHotReload();
    clouds.shaderHotReload(sharedRenderDescriptors.getLayout());

    if (device->supportsHardwareAcceleratedRRT()) {
        std::array<vk::DescriptorSetLayout, 2> const layouts = { sharedRenderDescriptors.getLayout(),
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
        fatal_frame_error = true;
        if (error_code == vk::Result::eErrorDeviceLost) { device_lost_detected = true; }
        if (window != nullptr && window->get_window() != nullptr) {
            glfwSetWindowShouldClose(window->get_window(), GLFW_TRUE);
        }
        end_imgui_frame_if_needed();
    };

    // Used by every RECOVERABLE post-acquire early return (the app keeps
    // running and will hand imageAvailableSemaphore() straight back to the
    // next vkAcquireNextImageKHR call). recreateSwapChain() waits idle first,
    // so the acquire's signal operation has already completed by the time it
    // reaches createSynchronization() - the semaphore is signaled but has no
    // pending wait, which makes destroying and recreating it legal. That is
    // the only way to retire a semaphore this frame never waited on without
    // submitting a dummy batch.
    const auto abort_frame_after_acquire = [&](const char *message) -> void {
        spdlog::error(message);
        end_imgui_frame_if_needed();
        recreateSwapChain();
    };

    if (frameSync.frameSyncCount() == 0) {
        spdlog::error("No synchronization frames available; skipping draw frame.");
        end_imgui_frame_if_needed();
        return;
    }

    // Only consult (and clear) the resize flag once the frameSync guard has
    // already passed - checkChangedFramebufferSize() clears the flag as soon
    // as it observes it, so calling it while the guard would still block the
    // recreate swallows the resize: the flag is gone but recreateSwapChain()
    // never ran.
    if (frameSync.frameSyncCount() > 0 && !frameSync.inFlightFencesEmpty() && checkChangedFramebufferSize()) {
        recreateSwapChain();
    }

    if (frameSync.currentFrame() >= frameSync.inFlightFenceCount()
        || frameSync.currentFrame() >= frameSync.imageAvailableCount()) {
        spdlog::error(fmt::format("Frame synchronization index out of range: {}", frameSync.currentFrame()));
        end_imgui_frame_if_needed();
        return;
    }

    if (!frameSync.inFlightFence() || !frameSync.imageAvailableSemaphore()) {
        spdlog::error(fmt::format("Synchronization handles are invalid for frame {}.", frameSync.currentFrame()));
        fatal_frame_error = true;
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
        abort_frame_after_acquire("Swapchain out of date at acquire; recreating.");
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
        abort_frame_after_acquire(
          fmt::format("Swapchain image index out of range: {}", image_index).c_str());
        return;
    }

    if (image_index >= frameSync.renderFinishedCount() || !frameSync.renderFinishedSemaphore(image_index)) {
        abort_frame_after_acquire(
          fmt::format("Render-finished semaphore missing for swapchain image {}.", image_index).c_str());
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

    if (!update_uniform_buffers(image_index)) {
        // The command buffer is already recording (begin() above succeeded);
        // rendering this frame against uniforms that were never written is
        // worse than dropping it, but create_command_buffers() (called from
        // recreateSwapChain()) must not free a buffer still in the recording
        // state.
        std::ignore = command_buffers[image_index].end();
        abort_frame_after_acquire("Failed to update uniform buffers; dropping this frame.");
        return;
    }

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();
    const bool raytracing_available = device->supportsHardwareAcceleratedRRT();
    const char *const render_mode =
      (!raytracing_available || (!guiRendererSharedVars.raytracing && !guiRendererSharedVars.pathTracing))
        ? "rasterizer"
        : (guiRendererSharedVars.raytracing ? "raytracing" : "path_tracing");
    if (raytracing_available && guiRendererSharedVars.raytracing) { update_raytracing_descriptor_set(image_index); }

    if (!record_commands(image_index, guiSceneSharedVars)) {
        std::ignore = command_buffers[image_index].end();
        abort_frame_after_acquire("record_commands failed; dropping this frame.");
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

    // dirShadowMap is sized per swapchain image (its light-matrices buffer
    // vector and lightMatricesDescriptors set) and CascadedShadowMap::init
    // caches sharedRenderDescriptors' layout, so it must be re-provisioned
    // whenever this method runs - and only after initDescriptorResources()
    // just replaced that layout, or it would cache the layout about to be
    // destroyed.
    reinitShadowMapForCurrentSettings();

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
    deferredRasterizer.recreateFrameResources();
    clouds.recreateFrameResources(graphics_command_pool, vulkanSwapChain.getSwapChainExtent().width, vulkanSwapChain.getSwapChainExtent().height);

    // The accumulation history is swapchain-extent-sized; recreate it (which
    // also resets the frame counter - the old history is meaningless at the
    // new resolution). updateAllDescriptorSets() below rewrites its binding.
    if (device->supportsHardwareAcceleratedRRT()) { createPathTracingAccumulationResources(); }

    skyBox.recreateFrameResources(swapchainImageViews(), postStage.getDepthBufferImageView(),
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

bool Kataglyphis::VulkanRenderer::update_uniform_buffers(uint32_t image_index)
{
    if (image_index >= globalUBOMapped.size() || image_index >= sceneUBOMapped.size()) {
        spdlog::error(fmt::format("Uniform buffer index out of range: {}", image_index));
        return false;
    }

    std::memcpy(globalUBOMapped[image_index], &globalUBO, sizeof(VulkanRendererInternals::GlobalUBO));
    std::memcpy(sceneUBOMapped[image_index], &sceneUBO, sizeof(VulkanRendererInternals::SceneUBO));

    // Same per-image-index buffering as the two UBOs above: the shadow-render
    // matrices (this) and the shadow-sample matrices (sceneUBO, filled in
    // updateUniforms) must land in the SAME image's buffers together.
    dirShadowMap.uploadLightMatrices(image_index);
    return true;
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

    // The added model is NEW geometry, so its BLAS must be built and the TLAS
    // rebuilt to reference it - otherwise it loads and renders in the raster
    // paths (which iterate the scene directly) but is INVISIBLE to RT/PT, which
    // only see the acceleration structure. Unlike a transform change (TLAS-only),
    // new geometry needs the BLAS too, so this is the full createASForScene
    // (it clears the old BLAS/TLAS first, so a rebuild is safe).
    refreshAfterSceneChange(true);
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

    if (!asManager.getTLAS()) {
        return;
    }

    writeRaytracingDescriptorsForImage(image_index);
}

void Kataglyphis::VulkanRenderer::writeRaytracingDescriptorsForImage(uint32_t image_index)
{
    vk::AccelerationStructureKHR &vulkanTLAS = asManager.getTLAS();
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

std::vector<vk::ImageView> Kataglyphis::VulkanRenderer::swapchainImageViews()
{
    std::vector<vk::ImageView> views(vulkanSwapChain.getNumberSwapChainImages());
    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        views[i] = vulkanSwapChain.getSwapChainImage(i).getImageView();
    }
    return views;
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

    const std::array<vk::DescriptorSet, 1> rasterizer_descriptor_sets = { sharedRenderDescriptors.sets()[image_index] };

    if (guiSceneSharedVars.clouds_enabled) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "clouds", { 0.80F, 0.85F, 0.95F, 1.0F });
        write_pass_timestamp(GpuTimedPass::Clouds, true);

        // Cross-frame WAR: cloudOutputTexture is a SINGLE image (not duplicated
        // per frame-in-flight), so this frame's compute write must be ordered
        // after the previous frame's post-pass fragment-shader read of it. A
        // pipeline barrier orders against ALL previously submitted commands on
        // this queue, not just the current command buffer, so recording it here
        // closes the gap even though MAX_FRAME_DRAWS == 3 (common/Globals.hpp)
        // means the fence this frame waits on (FrameSync::inFlightFence()) only
        // guarantees the submission 3 frames prior has completed, not the
        // immediately preceding one. A write-after-read needs only an execution
        // dependency, which is why srcAccessMask is empty here too.
        vk::ImageMemoryBarrier cloud_output_war_barrier{};
        cloud_output_war_barrier.oldLayout = vk::ImageLayout::eGeneral;
        cloud_output_war_barrier.newLayout = vk::ImageLayout::eGeneral;
        cloud_output_war_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cloud_output_war_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cloud_output_war_barrier.image = clouds.getCloudOutputTexture()->getImage();
        cloud_output_war_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        cloud_output_war_barrier.subresourceRange.baseMipLevel = 0;
        cloud_output_war_barrier.subresourceRange.levelCount = 1;
        cloud_output_war_barrier.subresourceRange.baseArrayLayer = 0;
        cloud_output_war_barrier.subresourceRange.layerCount = 1;
        cloud_output_war_barrier.srcAccessMask = {};
        cloud_output_war_barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
          vk::PipelineStageFlagBits::eComputeShader,
          {},
          nullptr,
          nullptr,
          cloud_output_war_barrier);

        clouds.recordComputeCommands(commandBuffer, rasterizer_descriptor_sets);

        // Order this compute write before the post pass's fragment-shader read of
        // cloudOutputTexture (bound at binding 1, eGeneral, in updatePostDescriptorSets).
        // PostStage's only subpass dependency is eColorAttachmentOutput ->
        // eColorAttachmentOutput (PostStage.cpp) and cannot order a compute-shader
        // write, unlike the same-layout swapchain barrier removed below (that one
        // WAS a colour-attachment write, so the render pass dependency covered it).
        // Written by hand rather than via VulkanImage::transitionImageLayout's
        // eGeneral->eGeneral overload: that helper derives both stages from the
        // layout alone, giving eAllCommands -> eAllCommands (a full pipeline stall
        // every frame) for what only needs one compute-to-fragment edge.
        vk::ImageMemoryBarrier cloud_output_barrier{};
        cloud_output_barrier.oldLayout = vk::ImageLayout::eGeneral;
        cloud_output_barrier.newLayout = vk::ImageLayout::eGeneral;
        cloud_output_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cloud_output_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cloud_output_barrier.image = clouds.getCloudOutputTexture()->getImage();
        cloud_output_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        cloud_output_barrier.subresourceRange.baseMipLevel = 0;
        cloud_output_barrier.subresourceRange.levelCount = 1;
        cloud_output_barrier.subresourceRange.baseArrayLayer = 0;
        cloud_output_barrier.subresourceRange.layerCount = 1;
        cloud_output_barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        cloud_output_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
          vk::PipelineStageFlagBits::eFragmentShader,
          {},
          nullptr,
          nullptr,
          cloud_output_barrier);

        // Cross-frame WAR closed: the eFragmentShader -> eComputeShader barrier
        // recorded above, before recordComputeCommands, orders this frame's
        // compute write against the PREVIOUS frame's post-pass fragment-shader
        // read of the same single cloudOutputTexture. A pipeline barrier orders
        // against all previously submitted commands on the queue, not just the
        // current command buffer, so it closes the gap even though
        // MAX_FRAME_DRAWS == 3 (common/Globals.hpp) means the fence this frame
        // waits on (FrameSync::inFlightFence()) only guarantees the submission 3
        // frames prior has completed, not the immediately preceding one.
        // Measured 2026-08-01, with this barrier in place (RX 9070 XT,
        // Run-SyncValidation.ps1, khronos_validation.validate_sync=true): no
        // SYNC-HAZARD in the log across the new
        // GoldenRender.CloudsAcrossManyFramesDoesNotLoseTheDevice (30+ frames,
        // clouds enabled) nor in the frames the all-maximum case of
        // GuiInputSweepNeverCrashesOrLosesTheDevice completes before hitting the
        // unrelated pre-existing path-tracing VK_ERROR_DEVICE_LOST bug (see
        // BACKLOG.md). The pre-fix state was not separately re-measured under
        // sync validation, so treat this as closing a real spec gap rather than
        // as a confirmed-observed-then-fixed hazard.
        write_pass_timestamp(GpuTimedPass::Clouds, false);
    }

    {
        Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &mutable_gui_vars =
          gui->getGuiRendererSharedVars();
        if (guiSceneSharedVars.shadows_enabled) {
            Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "shadow_cascades", { 0.55F, 0.35F, 0.10F, 1.0F });
            write_pass_timestamp(GpuTimedPass::ShadowCascades, true);
            dirShadowMap.recordCommands(
              commandBuffer, image_index, scene, rasterizer_descriptor_sets, guiRendererSharedVars.frustum_culling_enabled);
            write_pass_timestamp(GpuTimedPass::ShadowCascades, false);
            mutable_gui_vars.visibility.shadow_casters_drawn = dirShadowMap.getCastersDrawn();
            mutable_gui_vars.visibility.shadow_casters_total = dirShadowMap.getCastersConsidered();
        } else {
            mutable_gui_vars.visibility.shadow_casters_drawn = 0;
            mutable_gui_vars.visibility.shadow_casters_total = 0;
        }
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

    if (!raytracingOwnsFrame(image_index)) {
        recordRasterPass(commandBuffer, image_index, rasterizer_descriptor_sets, camera_frustum);
    } else {
        Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &mutable_gui_vars =
          gui->getGuiRendererSharedVars();
        mutable_gui_vars.visibility.meshes_drawn = 0;
        mutable_gui_vars.visibility.meshes_total = 0;
    }

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
        const std::array<vk::DescriptorSet, 1> post_descriptor_sets = { postDescriptors.sets()[image_index] };
        postStage.recordCommands(commandBuffer, image_index, post_descriptor_sets, guiSceneSharedVars.clouds_enabled);
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
  std::span<const vk::DescriptorSet> rasterizer_descriptor_sets,
  const std::optional<FrustumPlanes> &camera_frustum)
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    if (guiRendererSharedVars.rasterizationMode == Kataglyphis::VulkanRendererInternals::FrontendShared::RasterizationMode::Forward) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "forward", { 0.20F, 0.60F, 1.00F, 1.0F });
        rasterizer.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets, camera_frustum);
    } else {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "deferred", { 0.20F, 0.40F, 0.80F, 1.0F });
        const std::array<vk::DescriptorSet, 2> deferred_sets = { sharedRenderDescriptors.sets()[image_index], gbufferDescriptors.sets()[image_index] };
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

bool Kataglyphis::VulkanRenderer::raytracingOwnsFrame(uint32_t image_index)
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    // The TLAS-null term covers the async model-load window: with RT/PT
    // enabled before the scene arrives, RT/PT cannot dispatch yet, so the
    // raster pass must still run.
    return device->supportsHardwareAcceleratedRRT() && image_index < raytracingDescriptors.sets().size()
           && asManager.getTLAS() && (guiRendererSharedVars.raytracing || guiRendererSharedVars.pathTracing);
}

void Kataglyphis::VulkanRenderer::recordRaytracingOrPathTracing(vk::CommandBuffer &commandBuffer, uint32_t image_index)
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    // The TLAS guard covers the async model-load window: with RT/PT enabled
    // before the scene arrives, the record path used to dispatch against
    // descriptor sets that were never written (TLAS, output, accumulation) -
    // 20 validation errors in the pre-load frames of the accumulation golden.
    if (!raytracingOwnsFrame(image_index)) { return; }

    const std::array<vk::DescriptorSet, 2> raytracing_descriptor_sets = { sharedRenderDescriptors.sets()[image_index],
        raytracingDescriptors.sets()[image_index] };

    if (guiRendererSharedVars.raytracing) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "raytracing", { 0.85F, 0.25F, 0.55F, 1.0F });
        Texture &renderResult = activeOffscreenTexture(image_index);
        raytracingStage.recordCommands(
          commandBuffer, renderResult.getVulkanImage(), raytracing_descriptor_sets);
    } else if (guiRendererSharedVars.pathTracing) {
        Kataglyphis::debug::ScopedCmdLabel const label(commandBuffer, "pathtracing", { 0.60F, 0.25F, 0.85F, 1.0F });
        Texture &renderResult = activeOffscreenTexture(image_index);

        // A camera, light or quality change invalidates the accumulated
        // history; restart the running mean from this frame. The light is
        // part of the key because path_tracing.slang's NEE terms (:227,
        // :253-255) read sceneUBO.dirLight, so a light change makes the
        // running mean blend samples lit by two different lights.
        const Kataglyphis::VulkanRendererInternals::PathTracingHistoryKey current_history{
            .view = camera->calculate_viewmatrix(),
            .lightDirection = sceneUBO.dirLight.direction,
            .lightColorAndRadiance = sceneUBO.dirLight.color,
            .samplesPerPixel = guiRendererSharedVars.pathTracingSamplesPerPixel,
            .maxBounces = guiRendererSharedVars.pathTracingMaxBounces,
        };
        if (current_history != pathTracingLastHistory) {
            pathTracingAccumulatedFrames = 0;
            pathTracingLastHistory = current_history;
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
    Kataglyphis::VulkanRendererInternals::createColorAttachment(
      pathTracingAccumulation, device, extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eStorage);

    // Storage images live in eGeneral for their whole lifetime (same pattern
    // as the clouds output texture).
    vk::CommandBuffer commandBuffer = Kataglyphis::VulkanRendererInternals::CommandBufferManager::beginCommandBuffer(
      device->getLogicalDevice(), graphics_command_pool);
    if (!commandBuffer) {
        spdlog::error("Failed to begin command buffer for path tracing accumulation image transition!");
        pathTracingAccumulation.cleanUp();
        return;
    }
    pathTracingAccumulation.getVulkanImage().transitionImageLayout(
      commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 1, vk::ImageAspectFlagBits::eColor);
    static_cast<void>(Kataglyphis::VulkanRendererInternals::CommandBufferManager::endAndSubmitCommandBuffer(
      device->getLogicalDevice(), graphics_command_pool, device->getGraphicsQueue(), commandBuffer));

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

    // objectDescriptions holds one entry per MESH, flattened across models;
    // every mesh of a model shares that model's offset into the flattened
    // global texture array. The shaders add this to the model-LOCAL material
    // textureIDs.
    Kataglyphis::assignTextureOffsets(
      objectDescriptions, scene->getMeshCountPerModel(), scene->getTextureCountPerModel());

    if (!objectDescriptions.empty()) {
        if (!vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
              graphics_command_pool,
              objectDescriptionBuffer,
              vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
              vk::MemoryPropertyFlagBits::eDeviceLocal,
              objectDescriptions)) {
            spdlog::error(
              "VulkanRenderer::create_object_description_buffer: upload failed; object-description buffer left "
              "unwritten.");
        }
    } else {
        // Create an empty buffer (1 byte) if no object descriptions are present to avoid validation error
        if (!vulkanBufferManager.createBufferAndUploadVectorOnDevice(device,
              graphics_command_pool,
              objectDescriptionBuffer,
              vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
              vk::MemoryPropertyFlagBits::eDeviceLocal,
              std::vector<uint32_t>{0})) {
            spdlog::error(
              "VulkanRenderer::create_object_description_buffer: empty placeholder buffer upload failed.");
        }
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

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) { writeRaytracingDescriptorsForImage(i); }
}

void Kataglyphis::VulkanRenderer::createSharedRenderDescriptorResources()
{
    // The shared render descriptor set binds MAX_TEXTURE_COUNT sampled images
    // and samplers in a single stage; a device below either per-stage limit
    // cannot honour that fixed-size array. The array size is a compile-time
    // shader constant, so this is a warning, not a runtime shrink - shrinking
    // the host-side array would desynchronise host and device.
    if (const uint32_t max_sampled_images = device->getMaxPerStageDescriptorSampledImages();
        max_sampled_images < static_cast<uint32_t>(MAX_TEXTURE_COUNT)) {
        spdlog::critical(
          "Device maxPerStageDescriptorSampledImages ({}) is below MAX_TEXTURE_COUNT ({}) - texture binding may fail.",
          max_sampled_images,
          MAX_TEXTURE_COUNT);
    }
    if (const uint32_t max_samplers = device->getMaxPerStageDescriptorSamplers();
        max_samplers < static_cast<uint32_t>(MAX_TEXTURE_COUNT)) {
        spdlog::critical(
          "Device maxPerStageDescriptorSamplers ({}) is below MAX_TEXTURE_COUNT ({}) - texture binding may fail.",
          max_samplers,
          MAX_TEXTURE_COUNT);
    }

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
      .addBinding(SHADOW_MAP_BINDING, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);

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
    // texture_offset (see Kataglyphis::assignTextureOffsets, the other half
    // of this invariant). Scene::getTextureCountPerModel() is the shared
    // accessor that guarantees both halves agree; this function used to bind
    // model 0's textures only, so any added model shaded with the FIRST
    // model's images (its local textureIDs collided with model 0's slots).
    const Kataglyphis::FlattenedTexturePlan plan = Kataglyphis::planFlattenedTextureSlots(
      scene->getTextureCountPerModel(), static_cast<uint32_t>(MAX_TEXTURE_COUNT));

    if (plan.exhausted) {
        spdlog::warn(
          "Texture slots exhausted: {} textures across {} models exceed MAX_TEXTURE_COUNT={} - "
          "models past the cap will sample the wrong slots.",
          plan.requestedCount,
          scene->getModelCount(),
          MAX_TEXTURE_COUNT);
    }

    if (plan.slots.empty()) { return; }

    std::vector<vk::DescriptorImageInfo> image_info_textures;
    std::vector<vk::DescriptorImageInfo> image_info_texture_sampler;
    image_info_textures.reserve(plan.slots.size());
    image_info_texture_sampler.reserve(plan.slots.size());

    for (const Kataglyphis::FlattenedTextureSlot &slot : plan.slots) {
        vk::DescriptorImageInfo texture_info{};
        texture_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        texture_info.imageView = scene->getTextures(slot.model)[slot.indexInModel].getImageView();
        image_info_textures.push_back(texture_info);

        vk::DescriptorImageInfo sampler_info{};
        sampler_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        sampler_info.sampler = scene->getTextureSampler(slot.model)[slot.indexInModel];
        image_info_texture_sampler.push_back(sampler_info);
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
