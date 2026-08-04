// Shared scene types: structs and binding constants used by BOTH the
// rasterization and ray tracing shaders. This module has NO ray-tracing-
// specific types (HitPayload, PushConstantRaytracing) — those live in
// raytracing/rt_types.slang, which imports this module. This separation
// prevents rasterization shaders from pulling in ray tracing capabilities
// (which conflict with `discard` in fragment shaders).

static const int MAX_TEXTURE_COUNT = 128;

// ----- MAIN RENDER DESCRIPTOR SET (shared between rasterizer and raytracer)
static const int globalUBO_BINDING = 0;
static const int sceneUBO_BINDING = 1;
static const int OBJECT_DESCRIPTION_BINDING = 2;
static const int TEXTURES_BINDING = 3;
static const int SAMPLER_BINDING = 4;
static const int SHADOW_MAP_BINDING = 5;

// ---- RAYTRACING BINDING (only used by RT/path-tracing shaders)
static const int TLAS_BINDING = 0;
static const int OUT_IMAGE_BINDING = 1;
static const int ACCUMULATION_IMAGE_BINDING = 2;

// Vertex layout (matches Src/shared/scene/Vertex.hpp).
struct Vertex
{
    float3 position;
    float3 normal;
    float4 color;
    float2 texture_coords;
};

// Material (matches Src/shared/scene/ObjMaterial.hpp, scalar layout).
struct ObjMaterial
{
    float3 diffuse;
    float3 emission;
    float shininess;
    float dissolve;
    int textureID;
    float alphaCutoff;
    float3 uv_transform_row0;
    float3 uv_transform_row1;
    float metallic;
};

// Per-mesh object description (matches ObjectDescription.hpp). The uint64_t
// fields are Vulkan buffer device addresses, cast to T* in the shader.
struct ObjectDescription
{
    uint64_t vertex_address;
    uint64_t index_address;
    uint64_t material_index_address;
    uint64_t material_address;
    uint64_t texture_offset;
};

// Buffer-reference types: Slang T* maps to PhysicalStorageBuffer in SPIR-V
// (GL_EXT_buffer_reference2). Cast from uint64_t device addresses.
struct Vertices { Vertex v[]; }
struct Indices { int3 i[]; }
struct MaterialIDs { int i[]; }
struct Materials { ObjMaterial m[]; }

// Flattened global-texture-array index for a material's textureID within its
// model's slot range. Clamped rather than bounds-checked: a model whose
// texture count pushes it past MAX_TEXTURE_COUNT silently samples a wrong
// slot instead of going out of bounds (see VulkanRenderer.cpp's
// "models past the cap will sample the wrong slots" warning).
int resolve_texture_slot(ObjectDescription obj, ObjMaterial material)
{
    return clamp(int(obj.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
}

static const int MAX_CASCADES = 3;
static const int MAX_PCF_RADIUS = 20;

struct DirectionalLightData
{
    float4 direction;
    float4 color; // w = radiance
};

struct GlobalUBO
{
    float4x4 projection;
    float4x4 view;
    float4x4 inv_projection;
    float4x4 inv_view;
};

struct SceneUBO
{
    DirectionalLightData dirLight;
    uint pcfRadius;
    float cascadedShadowIntensity;
    uint numCascades;
    float4 cascadeSplits;
    float4x4 cascadeLightSpaceMatrices[MAX_CASCADES];
    float4 view_dir;
    float4 cam_pos;
    float4 cloudLightMarch;
    float4 cloudMeshScale;
    float4 cloudMeshOffset;
    float4 cloudParameters;
};