#ifndef HOST_DEVICE_SHARED_VARS
#define HOST_DEVICE_SHARED_VARS

const int MAX_TEXTURE_COUNT = 24;

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

#endif
