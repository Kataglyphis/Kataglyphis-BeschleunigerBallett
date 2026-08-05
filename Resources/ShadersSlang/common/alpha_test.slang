import scene_types;
import base_color;
import material_rules;

// Shared glTF MASK alpha test for ray queries: raytrace.rahit.slang's
// any-hit shader and path_tracing.slang's ray-query candidate loop (which
// has no any-hit stage of its own) both alpha-test a candidate/committed
// triangle against this predicate, mirroring shadow_map.slang's rasterizer
// alpha test and material_rules.slang's alpha_masked_out() product rule
// (baseColorFactor.a * baseColorTexture.a * COLOR_0.a), which this module
// now calls directly rather than mirroring.
//
// Declares its own textures/textureSamplers bindings rather than importing
// material_fetch.slang for them: path_tracing.slang already declares its
// own objectDescription binding directly (see base_color.slang's comment
// on the same constraint), so importing material_fetch.slang here - which
// declares objectDescription again - would make that binding ambiguous the
// moment path_tracing.slang imports this module.

[vk::binding(TEXTURES_BINDING, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];
[vk::binding(SAMPLER_BINDING, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];

// True when a ray hit's triangle should be treated as transparent: a glTF
// MASK material whose sampled alpha falls below its cutoff. Non-MASK
// materials (alphaCutoff < 0) always return false.
bool ray_hit_masked_out(ObjectDescription obj, uint primitiveID, float2 bary)
{
    MaterialIDs* materialIDs = (MaterialIDs*)obj.material_index_address;
    Materials* materials = (Materials*)obj.material_address;
    ObjMaterial material = materials->m[materialIDs->i[primitiveID]];

    if (material.alphaCutoff < 0.0) { return false; }

    Vertices* vertices = (Vertices*)obj.vertex_address;
    Indices* indices = (Indices*)obj.index_address;
    int3 ind = indices->i[primitiveID];

    float3 barycentrics = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

    float vertexAlpha = vertices->v[ind.x].color.a * barycentrics.x
                       + vertices->v[ind.y].color.a * barycentrics.y
                       + vertices->v[ind.z].color.a * barycentrics.z;

    float sampledAlpha = vertexAlpha;
    if (material.textureID >= 0 || material.alphaTextureID >= 0)
    {
        float2 texCoords = vertices->v[ind.x].texture_coords * barycentrics.x
                          + vertices->v[ind.y].texture_coords * barycentrics.y
                          + vertices->v[ind.z].texture_coords * barycentrics.z;
        // Explicit LOD: ray queries have no fragment derivatives, so an
        // implicit-LOD .Sample() fails SPIR-V validation (same rule as
        // raytrace.rchit.slang's closesthit sample).
        if (material.textureID >= 0)
        {
            int textureId = resolve_texture_slot(obj, material);
            sampledAlpha *= textures[textureId].SampleLevel(textureSamplers[textureId], transform_uv(texCoords, material), 0.0).a;
        }
        if (material.alphaTextureID >= 0)
        {
            int alphaTexId = resolve_texture_slot(obj, material.alphaTextureID);
            sampledAlpha *= textures[alphaTexId].SampleLevel(textureSamplers[alphaTexId], transform_uv(texCoords, material), 0.0).r;
        }
    }

    return alpha_masked_out(material, sampledAlpha);
}
