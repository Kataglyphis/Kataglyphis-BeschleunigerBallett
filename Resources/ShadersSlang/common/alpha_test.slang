import scene_types;
import base_color;
import material_rules;
import material_textures;

// Shared glTF MASK alpha test for ray queries: raytrace.rahit.slang's
// any-hit shader and path_tracing.slang's ray-query candidate loop (which
// has no any-hit stage of its own) both alpha-test a candidate/committed
// triangle against this predicate, mirroring shadow_map.slang's rasterizer
// alpha test and material_rules.slang's alpha_masked_out() product rule
// (baseColorFactor.a * baseColorTexture.a * COLOR_0.a), which this module
// now calls directly rather than mirroring.
//
// Imports its textures/textureSamplers bindings from material_textures.slang
// rather than declaring its own copy (material_fetch.slang declares only
// objectDescription, not textures, so that module still cannot supply them):
// ray_hit_masked_out() takes its ObjectDescription as a parameter rather
// than fetching it itself, so this module has no need to import
// material_fetch.slang at all. Both raytrace.rahit.slang and
// path_tracing.slang import material_fetch and alpha_test together without
// conflict.

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
            sampledAlpha *= sample_alpha_lod0(obj, material, texCoords);
        }
    }

    return alpha_masked_out(material, sampledAlpha);
}
