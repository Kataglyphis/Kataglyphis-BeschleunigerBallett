import scene_types;
import rt_types;
import material_fetch;
import base_color;

// Ray tracing any-hit shader. Sole source for this shader. Emits SPIR-V
// only.
//
// Alpha-tests glTF MASK materials against the sampled base-colour texture
// alpha and vertex COLOR_0 alpha, mirroring shadow_map.slang's rasterizer
// alpha test (same outer alphaCutoff guard, same texture/no-texture split).
// Shared by both the primary ray's hit group and the shadow ray
// (raytrace.rchit.slang's shadow TraceRay), so it must never write
// `payload` - the shadow ray's payload is a `bool`, not a `HitPayload`.

[vk::binding(3, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];
[vk::binding(4, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];

[shader("anyhit")]
void rahit_main(
    inout HitPayload payload,
    in BuiltInTriangleIntersectionAttributes attribs)
{
    // Per-mesh object description and material: same lookup as rchit_main.
    uint objIndex = InstanceIndex() + GeometryIndex();
    ObjectDescription obj = fetch_object_description(objIndex);
    MaterialIDs* materialIDs = (MaterialIDs*)obj.material_index_address;
    Materials* materials = (Materials*)obj.material_address;
    ObjMaterial material = materials->m[materialIDs->i[PrimitiveIndex()]];

    // OPAQUE/BLEND (alphaCutoff < 0) never discards; skip the vertex fetch
    // and texture sample entirely for the common non-MASK case.
    if (material.alphaCutoff < 0.0) { return; }

    Vertices* vertices = (Vertices*)obj.vertex_address;
    Indices* indices = (Indices*)obj.index_address;
    int3 ind = indices->i[PrimitiveIndex()];

    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
                                 attribs.barycentrics.x,
                                 attribs.barycentrics.y);

    float vertexAlpha = vertices->v[ind.x].color.a * barycentrics.x
                       + vertices->v[ind.y].color.a * barycentrics.y
                       + vertices->v[ind.z].color.a * barycentrics.z;

    if (material.textureID >= 0)
    {
        float2 texCoords = vertices->v[ind.x].texture_coords * barycentrics.x
                          + vertices->v[ind.y].texture_coords * barycentrics.y
                          + vertices->v[ind.z].texture_coords * barycentrics.z;
        int textureId = resolve_texture_slot(obj, material);
        // Explicit LOD: any-hit has no fragment derivatives either (same rule
        // as raytrace.rchit.slang's closesthit sample), so an implicit-LOD
        // .Sample() fails SPIR-V validation.
        float alpha = textures[textureId].SampleLevel(textureSamplers[textureId], transform_uv(texCoords, material), 0.0).a;
        if (alpha_masked_out(material, alpha * vertexAlpha)) { IgnoreHit(); }
    }
    else
    {
        if (alpha_masked_out(material, vertexAlpha)) { IgnoreHit(); }
    }
}
