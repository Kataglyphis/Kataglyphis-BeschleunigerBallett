#ifndef HOST_DEVICE_SHARED_VARS
#define HOST_DEVICE_SHARED_VARS

// Binding constants shared between the C++ host code and the Slang shaders.
// These mirror the [vk::binding(N, M)] annotations in
// Resources/ShadersSlang/common/scene_types.slang.

const int MAX_TEXTURE_COUNT = 128;
const int MAX_CASCADES = 3;
const int MAX_PCF_RADIUS = 20;

// ----- MAIN RENDER DESCRIPTOR SET ----- START (shared between rasterizer and
// raytracer)
#define globalUBO_BINDING 0
#define sceneUBO_BINDING 1
#define OBJECT_DESCRIPTION_BINDING 2
#define TEXTURES_BINDING 3
#define SAMPLER_BINDING 4
#define SHADOW_MAP_BINDING 5
// ----- MAIN RENDER DESCRIPTOR SET ----- END

// ---- RAYTRACING BINDING ---- START
#define TLAS_BINDING 0
#define OUT_IMAGE_BINDING 1
// Path-tracing temporal accumulation history (rgba32f storage image, one per
// renderer - NOT per swapchain image, it must persist across frames).
#define ACCUMULATION_IMAGE_BINDING 2
// ---- RAYTRACING BINDING ---- END

// ----- GBUFFER INPUT ATTACHMENT SET ----- START
// deferred.slang's lighting subpass reads the geometry subpass's outputs as
// input attachments in descriptor set 1. These are *input-attachment*
// indices (the vk::binding/vk::input_attachment_index value on the shader
// side and the gbufferDescriptors binding on the host side) - each must
// equal the position of its matching entry in DeferredRasterizer.cpp's
// lightingInputRefs. They are NOT the render pass's own attachment indices,
// which are these plus one (attachment 0 is the final colour output).
#define GBUFFER_NORMAL_BINDING 0
#define GBUFFER_ALBEDO_BINDING 1
#define GBUFFER_MATERIAL_BINDING 2
#define GBUFFER_DEPTH_BINDING 3
// ----- GBUFFER INPUT ATTACHMENT SET ----- END

#endif