module;
#include "common/Utilities.hpp"
#include "hostDevice/host_device_shared_vars.hpp"
#include "renderer/pushConstants/PushConstantPost.hpp"
#include "renderer/pushConstants/PushConstantRasterizer.hpp"
#include "renderer/pushConstants/PushConstantRayTracing.hpp"
#include "spdlog/spdlog.h"

#include <cstdint>
#include <glm/ext/matrix_clip_space.hpp>
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

module kataglyphis.vulkan.renderer;

import kataglyphis.vulkan.device;
import kataglyphis.vulkan.gui_renderer_shared_vars;
import kataglyphis.vulkan.gui_scene_shared_vars;
import kataglyphis.vulkan.object_description;
import kataglyphis.vulkan.queue_family_indices;
import kataglyphis.vulkan.debug;
import kataglyphis.vulkan.scene;
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
vk::Result toVkResult(VkResult result) { return static_cast<vk::Result>(result); }
}// namespace

Kataglyphis::VulkanRenderer::VulkanRenderer(Kataglyphis::Frontend::Window *window,
  Scene *scene,
  Kataglyphis::Frontend::GUI *gui,
  Camera *camera)
  : window(window), scene(scene), gui(gui)
{
    updateUniforms(scene, camera, window);

    instance = VulkanInstance();

    vk::DebugReportFlagsEXT const debugReportFlags =
      vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning;
    if (Kataglyphis::ENABLE_VALIDATION_LAYERS) {
        debug::setupDebuging(instance.getVulkanInstance(), debugReportFlags, nullptr);
    }

    create_surface();

    device = std::make_unique<VulkanDevice>(&instance, &surface);

    allocator = Allocator(device->getLogicalDevice(), device->getPhysicalDevice(), instance.getVulkanInstance());

    create_command_pool();

    vulkanSwapChain.initVulkanContext(device.get(), window, surface);
    create_uniform_buffers();
    create_command_buffers();

    createSynchronization();

    createSharedRenderDescriptorSetLayouts();
    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_rasterizer = { sharedRenderDescriptorSetLayout };
    rasterizer.init(device.get(), &vulkanSwapChain, descriptor_set_layouts_rasterizer, graphics_command_pool);
    create_post_descriptor_layout();
    std::vector<vk::DescriptorSetLayout> const descriptor_set_layouts_post = { post_descriptor_set_layout };
    postStage.init(device.get(), &vulkanSwapChain, descriptor_set_layouts_post);
    createDescriptorPoolSharedRenderStages();
    createSharedRenderDescriptorSet();

    updatePostDescriptorSets();

    std::vector<vk::DescriptorSetLayout> layouts;
    layouts.push_back(sharedRenderDescriptorSetLayout);
    if (device->supportsHardwareAcceleratedRRT()) {
        createRaytracingDescriptorPool();
        createRaytracingDescriptorSetLayouts();
        layouts.push_back(raytracingDescriptorSetLayout);
        raytracingStage.init(device.get(), layouts);
        pathTracing.init(device.get(), layouts);
    }

    scene->loadModel(device.get(), graphics_command_pool);
    updateTexturesInSharedRenderDescriptorSet();

    if (device->supportsHardwareAcceleratedRRT()) {
        asManager.createASForScene(device.get(), graphics_command_pool, scene);
    }

    create_object_description_buffer();

    if (device->supportsHardwareAcceleratedRRT()) {
        createRaytracingDescriptorSets();
        updateRaytracingDescriptorSets();
    }

    gui->initializeVulkanContext(device.get(),
      instance.getVulkanInstance(),
      postStage.getRenderPass(),
      graphics_command_pool,
      vulkanSwapChain.getNumberSwapChainImages());
    gui->setUserSelectionForRRT(device->supportsHardwareAcceleratedRRT());
}

void Kataglyphis::VulkanRenderer::updateUniforms(Scene *scene_data,
  Camera *camera_data,
  Kataglyphis::Frontend::Window *window_data)
{
    const GUISceneSharedVars guiSceneSharedVars = scene_data->getGuiSceneSharedVars();

    globalUBO.view = camera_data->calculate_viewmatrix();
    globalUBO.projection = glm::perspective(glm::radians(camera_data->get_fov()),
      static_cast<float>(window_data->get_width()) / static_cast<float>(window_data->get_height()),
      camera_data->get_near_plane(),
      camera_data->get_far_plane());

    sceneUBO.view_dir = glm::vec4(camera_data->get_camera_direction(), 1.0F);

    sceneUBO.light_dir = glm::vec4(guiSceneSharedVars.directional_light_direction[0],
      guiSceneSharedVars.directional_light_direction[1],
      guiSceneSharedVars.directional_light_direction[2],
      1.0F);

    sceneUBO.cam_pos = glm::vec4(camera_data->get_camera_position(), camera_data->get_fov());
}

void Kataglyphis::VulkanRenderer::updateStateDueToUserInput(Kataglyphis::Frontend::GUI *frontend_gui)
{
    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars &guiRendererSharedVars =
      frontend_gui->getGuiRendererSharedVars();

    if (guiRendererSharedVars.shader_hot_reload_triggered) {
        shaderHotReload();
        guiRendererSharedVars.shader_hot_reload_triggered = false;
    }
}

void Kataglyphis::VulkanRenderer::finishAllRenderCommands() { device->getLogicalDevice().waitIdle(); }

void Kataglyphis::VulkanRenderer::shaderHotReload()
{
    device->getLogicalDevice().waitIdle();

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
        end_imgui_frame_if_needed();
        return;
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
        return;
    }

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

    result = command_buffers[image_index].reset(0);
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

    if (result == vk::Result::eErrorOutOfDateKHR) {
        end_imgui_frame_if_needed();
        return;
    }

    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
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

void Kataglyphis::VulkanRenderer::update_uniform_buffers(uint32_t image_index)
{
    if (image_index >= globalUBOBuffer.size() || image_index >= sceneUBOBuffer.size()) {
        spdlog::error(fmt::format("Uniform buffer index out of range: {}", image_index));
        return;
    }

    std::vector<VulkanRendererInternals::GlobalUBO> global_ubo_data;
    global_ubo_data.push_back(globalUBO);

    std::vector<VulkanRendererInternals::SceneUBO> scene_ubo_data;
    scene_ubo_data.push_back(sceneUBO);

    VulkanBuffer stagingGlobalUBO;
    stagingGlobalUBO.create(device.get(),
      sizeof(VulkanRendererInternals::GlobalUBO),
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *mapped_global_ubo = device->getLogicalDevice().mapMemory(
      stagingGlobalUBO.getBufferMemory(), 0, sizeof(VulkanRendererInternals::GlobalUBO), {});
    std::memcpy(mapped_global_ubo, global_ubo_data.data(), sizeof(VulkanRendererInternals::GlobalUBO));
    device->getLogicalDevice().unmapMemory(stagingGlobalUBO.getBufferMemory());

    auto const copy_buffer_ref = static_cast<void (Kataglyphis::VulkanBufferManager::*)(
      vk::Device, vk::Queue, vk::CommandPool, VulkanBuffer &, VulkanBuffer &, vk::DeviceSize)>(
      &Kataglyphis::VulkanBufferManager::copyBuffer);
    (vulkanBufferManager.*copy_buffer_ref)(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      graphics_command_pool,
      stagingGlobalUBO,
      globalUBOBuffer[image_index],
      sizeof(VulkanRendererInternals::GlobalUBO));

    stagingGlobalUBO.cleanUp();

    VulkanBuffer stagingSceneUBO;
    stagingSceneUBO.create(device.get(),
      sizeof(VulkanRendererInternals::SceneUBO),
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *mapped_scene_ubo = device->getLogicalDevice().mapMemory(
      stagingSceneUBO.getBufferMemory(), 0, sizeof(VulkanRendererInternals::SceneUBO), {});
    std::memcpy(mapped_scene_ubo, scene_ubo_data.data(), sizeof(VulkanRendererInternals::SceneUBO));
    device->getLogicalDevice().unmapMemory(stagingSceneUBO.getBufferMemory());

    (vulkanBufferManager.*copy_buffer_ref)(device->getLogicalDevice(),
      device->getGraphicsQueue(),
      graphics_command_pool,
      stagingSceneUBO,
      sceneUBOBuffer[image_index],
      sizeof(VulkanRendererInternals::SceneUBO));

    stagingSceneUBO.cleanUp();
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
    descriptor_set_acceleration_structure.pAccelerationStructures = &vulkanTLAS;

    vk::WriteDescriptorSet write_descriptor_set_acceleration_structure{};
    write_descriptor_set_acceleration_structure.pNext = &descriptor_set_acceleration_structure;
    write_descriptor_set_acceleration_structure.dstSet = raytracingDescriptorSet[image_index];
    write_descriptor_set_acceleration_structure.dstBinding = TLAS_BINDING;
    write_descriptor_set_acceleration_structure.dstArrayElement = 0;
    write_descriptor_set_acceleration_structure.descriptorCount = 1;
    write_descriptor_set_acceleration_structure.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;

    vk::DescriptorImageInfo image_info{};
    Texture &renderResult = rasterizer.getOffscreenTexture(image_index);
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

    vk::CommandBuffer &commandBuffer = command_buffers[image_index];

    std::vector<vk::DescriptorSet> rasterizer_descriptor_sets = { sharedRenderDescriptorSet[image_index] };
    rasterizer.recordCommands(commandBuffer, image_index, scene, rasterizer_descriptor_sets);

    Kataglyphis::VulkanRendererInternals::FrontendShared::GUIRendererSharedVars const &guiRendererSharedVars =
      gui->getGuiRendererSharedVars();

    if (device->supportsHardwareAcceleratedRRT() && image_index < raytracingDescriptorSet.size()) {
        std::vector<vk::DescriptorSet> raytracing_descriptor_sets = { sharedRenderDescriptorSet[image_index],
            raytracingDescriptorSet[image_index] };

        if (guiRendererSharedVars.raytracing) {
            Texture &renderResult = rasterizer.getOffscreenTexture(image_index);
            raytracingStage.recordCommands(
              commandBuffer, renderResult.getVulkanImage(), &vulkanSwapChain, raytracing_descriptor_sets);
        } else if (guiRendererSharedVars.pathTracing) {
            Texture &renderResult = rasterizer.getOffscreenTexture(image_index);
            pathTracing.recordCommands(
              commandBuffer, image_index, renderResult.getVulkanImage(), &vulkanSwapChain, raytracing_descriptor_sets);
        }
    }

    std::vector<vk::DescriptorSet> post_descriptor_sets = { post_descriptor_set[image_index] };
    postStage.recordCommands(commandBuffer, image_index, post_descriptor_sets);

    return true;
}

void Kataglyphis::VulkanRenderer::cleanUpUBOs()
{
    for (VulkanBuffer &buffer : globalUBOBuffer) { buffer.cleanUp(); }
    for (VulkanBuffer &buffer : sceneUBOBuffer) { buffer.cleanUp(); }
    globalUBOBuffer.clear();
    sceneUBOBuffer.clear();
}

void Kataglyphis::VulkanRenderer::cleanUp()
{
    if (!device) { return; }

    device->getLogicalDevice().waitIdle();

    if (device->supportsHardwareAcceleratedRRT()) {
        pathTracing.cleanUp();
        raytracingStage.cleanUp();
        asManager.cleanUp();
    }

    rasterizer.cleanUp();
    postStage.cleanUp();

    objectDescriptionBuffer.cleanUp();

    cleanUpSync();
    cleanUpUBOs();
    cleanUpCommandPools();

    if (post_descriptor_pool) {
        device->getLogicalDevice().destroyDescriptorPool(post_descriptor_pool);
        post_descriptor_pool = nullptr;
    }
    if (post_descriptor_set_layout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(post_descriptor_set_layout);
        post_descriptor_set_layout = nullptr;
    }
    if (descriptorPoolSharedRenderStages) {
        device->getLogicalDevice().destroyDescriptorPool(descriptorPoolSharedRenderStages);
        descriptorPoolSharedRenderStages = nullptr;
    }
    if (sharedRenderDescriptorSetLayout) {
        device->getLogicalDevice().destroyDescriptorSetLayout(sharedRenderDescriptorSetLayout);
        sharedRenderDescriptorSetLayout = nullptr;
    }
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

    std::vector<vk::DescriptorSetLayoutBinding> layout_bindings = { post_sampler_layout_binding };

    vk::DescriptorSetLayoutCreateInfo layout_create_info{};
    layout_create_info.bindingCount = static_cast<uint32_t>(layout_bindings.size());
    layout_create_info.pBindings = layout_bindings.data();

    vk::Result result =
      device->getLogicalDevice().createDescriptorSetLayout(&layout_create_info, nullptr, &post_descriptor_set_layout);
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create descriptor set layout!")

    vk::DescriptorPoolSize post_pool_size{};
    post_pool_size.type = vk::DescriptorType::eCombinedImageSampler;
    post_pool_size.descriptorCount = vulkanSwapChain.getNumberSwapChainImages();

    std::vector<vk::DescriptorPoolSize> descriptor_pool_sizes = { post_pool_size };

    vk::DescriptorPoolCreateInfo pool_create_info{};
    pool_create_info.maxSets = vulkanSwapChain.getNumberSwapChainImages();
    pool_create_info.poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes.size());
    pool_create_info.pPoolSizes = descriptor_pool_sizes.data();

    result = device->getLogicalDevice().createDescriptorPool(&pool_create_info, nullptr, &post_descriptor_pool);
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create a descriptor pool!")

    post_descriptor_set.resize(vulkanSwapChain.getNumberSwapChainImages());

    std::vector<vk::DescriptorSetLayout> set_layouts(
      vulkanSwapChain.getNumberSwapChainImages(), post_descriptor_set_layout);

    vk::DescriptorSetAllocateInfo set_alloc_info{};
    set_alloc_info.descriptorPool = post_descriptor_pool;
    set_alloc_info.descriptorSetCount = vulkanSwapChain.getNumberSwapChainImages();
    set_alloc_info.pSetLayouts = set_layouts.data();

    result = device->getLogicalDevice().allocateDescriptorSets(&set_alloc_info, post_descriptor_set.data());
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create descriptor sets!")
    if (result != vk::Result::eSuccess) {
        post_descriptor_set.clear();
        return;
    }
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
        Texture &renderResult = rasterizer.getOffscreenTexture(static_cast<uint32_t>(i));
        image_info.imageView = renderResult.getImageView();
        image_info.sampler = postStage.getOffscreenSampler();

        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write.dstSet = post_descriptor_set[i];
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo = &image_info;

        device->getLogicalDevice().updateDescriptorSets(1, &descriptor_write, 0, nullptr);
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
    for (vk::Semaphore semaphore : render_finished_by_image) {
        if (semaphore) { device->getLogicalDevice().destroySemaphore(semaphore); }
    }

    for (uint32_t i = 0; i < frame_sync_count; i++) {
        if (image_available[i]) { device->getLogicalDevice().destroySemaphore(image_available[i]); }
        if (in_flight_fences[i]) { device->getLogicalDevice().destroyFence(in_flight_fences[i]); }
    }
}

void Kataglyphis::VulkanRenderer::create_object_description_buffer()
{
    std::vector<ObjectDescription> objectDescriptions = scene->getObjectDescriptions();

    vulkanBufferManager.createBufferAndUploadVectorOnDevice(device.get(),
      graphics_command_pool,
      objectDescriptionBuffer,
      vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      objectDescriptions);

    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
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
    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vk::WriteDescriptorSetAccelerationStructureKHR descriptor_set_acceleration_structure{};
        descriptor_set_acceleration_structure.pNext = nullptr;
        descriptor_set_acceleration_structure.accelerationStructureCount = 1;
        vk::AccelerationStructureKHR &vulkanTLAS = asManager.getTLAS();
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
        Texture &renderResult = rasterizer.getOffscreenTexture(static_cast<uint32_t>(i));
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

    vk::Result const result = device->getLogicalDevice().createDescriptorSetLayout(
      &layout_create_info, nullptr, &sharedRenderDescriptorSetLayout);
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create descriptor set layout!")
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
        ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create command pool!")
    }

    {
        vk::CommandPoolCreateInfo pool_info{};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = static_cast<uint32_t>(queue_family_indices.compute_family);

        vk::Result const result =
          device->getLogicalDevice().createCommandPool(&pool_info, nullptr, &compute_command_pool);
        ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create command pool!")
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

    image_available.resize(frame_sync_count);
    render_finished_by_image.resize(vulkanSwapChain.getNumberSwapChainImages());
    in_flight_fences.resize(frame_sync_count);
    images_in_flight_fences.resize(vulkanSwapChain.getNumberSwapChainImages());

    vk::SemaphoreCreateInfo semaphore_create_info{};

    vk::FenceCreateInfo fence_create_info{};
    fence_create_info.flags = vk::FenceCreateFlagBits::eSignaled;

    for (uint32_t i = 0; i < frame_sync_count; i++) {
        auto [image_available_result, image_available_handle] =
          device->getLogicalDevice().createSemaphore(semaphore_create_info);
        auto [in_flight_fence_result, in_flight_fence_handle] =
          device->getLogicalDevice().createFence(fence_create_info);

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
        auto [render_finished_result, render_finished_handle] =
          device->getLogicalDevice().createSemaphore(semaphore_create_info);

        if (render_finished_result != vk::Result::eSuccess || !render_finished_handle) {
            spdlog::error(fmt::format("Failed to create render-finished semaphore for swapchain image {} ({}).",
              image,
              static_cast<int>(render_finished_result)));
            frame_sync_count = 0;
            return;
        }

        render_finished_by_image[image] = render_finished_handle;
    }
}

void Kataglyphis::VulkanRenderer::create_uniform_buffers()
{
    globalUBOBuffer.resize(vulkanSwapChain.getNumberSwapChainImages());
    sceneUBOBuffer.resize(vulkanSwapChain.getNumberSwapChainImages());

    std::vector<VulkanRendererInternals::GlobalUBO> globalUBOdata;
    globalUBOdata.push_back(globalUBO);

    std::vector<VulkanRendererInternals::SceneUBO> sceneUBOdata;
    sceneUBOdata.push_back(sceneUBO);

    for (size_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vulkanBufferManager.createBufferAndUploadVectorOnDevice(device.get(),
          graphics_command_pool,
          globalUBOBuffer[i],
          vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
          vk::MemoryPropertyFlagBits::eDeviceLocal,
          globalUBOdata);

        vulkanBufferManager.createBufferAndUploadVectorOnDevice(device.get(),
          graphics_command_pool,
          sceneUBOBuffer[i],
          vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
          vk::MemoryPropertyFlagBits::eDeviceLocal,
          sceneUBOdata);
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

    vk::Result const result =
      device->getLogicalDevice().allocateDescriptorSets(&set_alloc_info, sharedRenderDescriptorSet.data());
    ASSERT_VULKAN(static_cast<VkResult>(result), "Failed to create descriptor sets!")
    if (result != vk::Result::eSuccess) {
        sharedRenderDescriptorSet.clear();
        return;
    }

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
    if (sharedRenderDescriptorSet.size() < vulkanSwapChain.getNumberSwapChainImages()) {
        spdlog::error("Shared render descriptor sets are not available; skipping texture update.");
        return;
    }

    std::vector<Texture> &modelTextures = scene->getTextures(0);
    const uint32_t scene_texture_count = scene->getTextureCount(0);
    const uint32_t texture_count_for_descriptors = std::min<uint32_t>(scene_texture_count, MAX_TEXTURE_COUNT);
    if (scene_texture_count > MAX_TEXTURE_COUNT) {
        spdlog::warn(fmt::format("Scene has {} textures, but MAX_TEXTURE_COUNT is {}. Clamping descriptor updates.",
          scene_texture_count,
          MAX_TEXTURE_COUNT));
    }

    if (texture_count_for_descriptors == 0) {
        spdlog::error("No textures available for descriptor update.");
        return;
    }

    std::vector<vk::DescriptorImageInfo> image_info_textures;
    image_info_textures.resize(MAX_TEXTURE_COUNT);
    for (uint32_t i = 0; i < texture_count_for_descriptors; i++) {
        image_info_textures[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image_info_textures[i].imageView = modelTextures[i].getImageView();
        image_info_textures[i].sampler = nullptr;
    }
    for (uint32_t i = texture_count_for_descriptors; i < MAX_TEXTURE_COUNT; i++) {
        image_info_textures[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image_info_textures[i].imageView = modelTextures[0].getImageView();
        image_info_textures[i].sampler = nullptr;
    }

    std::vector<vk::Sampler> &modelTextureSampler = scene->getTextureSampler(0);
    std::vector<vk::DescriptorImageInfo> image_info_texture_sampler;
    image_info_texture_sampler.resize(MAX_TEXTURE_COUNT);
    for (uint32_t i = 0; i < texture_count_for_descriptors; i++) {
        image_info_texture_sampler[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image_info_texture_sampler[i].imageView = nullptr;
        image_info_texture_sampler[i].sampler = modelTextureSampler[i];
    }
    for (uint32_t i = texture_count_for_descriptors; i < MAX_TEXTURE_COUNT; i++) {
        image_info_texture_sampler[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image_info_texture_sampler[i].imageView = nullptr;
        image_info_texture_sampler[i].sampler = modelTextureSampler[0];
    }

    for (uint32_t i = 0; i < vulkanSwapChain.getNumberSwapChainImages(); i++) {
        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write.dstSet = sharedRenderDescriptorSet[i];
        descriptor_write.dstBinding = TEXTURES_BINDING;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = vk::DescriptorType::eSampledImage;
        descriptor_write.descriptorCount = MAX_TEXTURE_COUNT;
        descriptor_write.pImageInfo = image_info_textures.data();

        vk::WriteDescriptorSet descriptor_sampler_write{};
        descriptor_sampler_write.dstSet = sharedRenderDescriptorSet[i];
        descriptor_sampler_write.dstBinding = SAMPLER_BINDING;
        descriptor_sampler_write.dstArrayElement = 0;
        descriptor_sampler_write.descriptorType = vk::DescriptorType::eSampler;
        descriptor_sampler_write.descriptorCount = MAX_TEXTURE_COUNT;
        descriptor_sampler_write.pImageInfo = image_info_texture_sampler.data();

        std::vector<vk::WriteDescriptorSet> write_descriptor_sets = { descriptor_write, descriptor_sampler_write };

        device->getLogicalDevice().updateDescriptorSets(
          static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
    }
}