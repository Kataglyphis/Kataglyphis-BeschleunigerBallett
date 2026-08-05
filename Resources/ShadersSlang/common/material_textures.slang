import scene_types;
import base_color;

// One owning module for the [vk::binding(TEXTURES_BINDING, 0)] / [vk::binding(SAMPLER_BINDING, 0)]
// texture arrays, previously declared separately in rasterizer.slang,
// deferred.slang, shadow_map.slang, raytrace.rchit.slang and
// alpha_test.slang, plus both the explicit-LOD (SampleLevel) and
// implicit-LOD (Sample) helpers for the four texture-slot sample blocks
// those five files also each repeated: alpha (map_d, i.e.
// ObjMaterial::alphaTextureID), normal, metallic-roughness and emissive.
// The base-colour (ObjMaterial::textureID) slot stays inline at each call
// site instead - it drives shading control flow (albedo assembly, discard,
// G-buffer packing) that differs per shader, where these four are a uniform
// "resolve slot, sample, return" pattern.
//
// One module, not two: probed by compiling a throwaway compute entry point
// and a throwaway closesthit entry point, each importing this module and
// calling only its explicit-LOD half - in both cases slangc emitted valid
// SPIR-V (spirv-val clean) with no extra capability, because the unused
// implicit-LOD functions are dead-code-eliminated per entry point before
// capability checking runs. Slang's capability checking is therefore
// per-function-reachable-from-the-entry-point, not whole-module, so the two
// LOD shapes can share one file safely. (A compute entry that *does* call an
// implicit-LOD helper compiles too - Slang adds
// SPV_KHR_compute_shader_derivatives / ComputeDerivativeGroupQuadsKHR for
// it - but no caller here does that.)
//
// Explicit LOD: ray tracing shading (raytrace.rchit.slang's closesthit,
// alpha_test.slang's ray_hit_masked_out used by raytrace.rahit.slang's
// any-hit and path_tracing.slang's ray-query candidate loop) runs under
// stages with no fragment derivatives, so these callers use the
// `_lod0` (SampleLevel) helpers. The rasterizer, deferred and shadow-map
// fragment shaders use the implicit-LOD helpers, matching their prior
// `.Sample(...)` call sites exactly.

[vk::binding(TEXTURES_BINDING, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];
[vk::binding(SAMPLER_BINDING, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];

// map_d: the alpha cut-out texture slot (ObjMaterial::alphaTextureID),
// distinct from the base-colour texture's own alpha channel. Callers guard
// on material.alphaTextureID >= 0 themselves, since the miss-case default
// (1.0, or omitted entirely) differs per call site.
float sample_alpha_lod0(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int alphaTexId = resolve_texture_slot(obj, material.alphaTextureID);
    return textures[alphaTexId].SampleLevel(textureSamplers[alphaTexId], transform_uv(texCoords, material), 0.0).r;
}

float sample_alpha(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int alphaTexId = resolve_texture_slot(obj, material.alphaTextureID);
    return textures[alphaTexId].Sample(textureSamplers[alphaTexId], transform_uv(texCoords, material)).r;
}

float3 sample_normal_lod0(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int normalTexId = resolve_texture_slot(obj, material.normalTextureID);
    return textures[normalTexId].SampleLevel(textureSamplers[normalTexId], normal_uv(texCoords, material), 0.0).xyz;
}

float3 sample_normal(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int normalTexId = resolve_texture_slot(obj, material.normalTextureID);
    return textures[normalTexId].Sample(textureSamplers[normalTexId], normal_uv(texCoords, material)).xyz;
}

float4 sample_metallic_roughness_lod0(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int metallicRoughnessTexId = resolve_texture_slot(obj, material.metallicRoughnessTextureID);
    return textures[metallicRoughnessTexId].SampleLevel(textureSamplers[metallicRoughnessTexId], metallic_roughness_uv(texCoords, material), 0.0);
}

float4 sample_metallic_roughness(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int metallicRoughnessTexId = resolve_texture_slot(obj, material.metallicRoughnessTextureID);
    return textures[metallicRoughnessTexId].Sample(textureSamplers[metallicRoughnessTexId], metallic_roughness_uv(texCoords, material));
}

float3 sample_emissive_lod0(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int emissiveTexId = resolve_texture_slot(obj, material.emissiveTextureID);
    return textures[emissiveTexId].SampleLevel(textureSamplers[emissiveTexId], emissive_uv(texCoords, material), 0.0).xyz;
}

float3 sample_emissive(ObjectDescription obj, ObjMaterial material, float2 texCoords)
{
    int emissiveTexId = resolve_texture_slot(obj, material.emissiveTextureID);
    return textures[emissiveTexId].Sample(textureSamplers[emissiveTexId], emissive_uv(texCoords, material)).xyz;
}
