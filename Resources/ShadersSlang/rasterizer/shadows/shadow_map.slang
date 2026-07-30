import scene_types;
import material_fetch;

// Cascaded shadow map shaders. Mirrors
// Resources/Shaders/rasterizer/shadows/directional_shadow_map.vert and .frag.
// Emits SPIR-V only. Single-pass layered cascade rendering: SV_ViewID IS
// the cascade index.

static const int NUM_CASCADES = 3;

[vk::binding(1, 1)] ConstantBuffer<float4x4[3]> lightSpaceMatrices;

struct ShadowPushConstants
{
    float4x4 model;
    uint objectIndex;
};
[[vk::push_constant]] ShadowPushConstants pc;

[vk::binding(3, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];
[vk::binding(4, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];

struct ShadowVsOut
{
    float4 svPosition : SV_Position;
    float2 fragUV : TEXCOORD0;
};

[shader("vertex")]
ShadowVsOut shadow_vs_main(
    float3 pos : POSITION,
    [[vk::location(3)]] float2 tex_coords : TEXCOORD0,
    uint viewId : SV_ViewID)
{
    ShadowVsOut o;
    uint cascade = min(viewId, uint(NUM_CASCADES - 1));
    o.fragUV = tex_coords;
    o.svPosition = mul(mul(lightSpaceMatrices[cascade], pc.model), float4(pos, 1.0));
    return o;
}

[shader("fragment")]
void shadow_fs_main(ShadowVsOut In, uint primitiveID : SV_PrimitiveID)
{
    ObjectDescription obj = fetch_object_description(pc.objectIndex);
    ObjMaterial material = fetch_material(obj, primitiveID);

    // Only textured MASK materials alpha-test; everything else casts unchanged.
    if (material.alphaCutoff >= 0.0 && material.textureID >= 0)
    {
        int textureId = clamp(int(obj.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
        float alpha = textures[textureId].Sample(textureSamplers[textureId], transform_uv(In.fragUV, material)).a;
        if (alpha < material.alphaCutoff) discard;
    }
}