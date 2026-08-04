import scene_types;
import material_fetch;

// Cascaded shadow map shaders. Sole source for this shader. Emits SPIR-V
// only. Single-pass layered cascade rendering: SV_ViewID IS the cascade
// index.

[vk::binding(1, 1)] ConstantBuffer<float4x4[MAX_CASCADES]> lightSpaceMatrices;

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
    uint cascade = min(viewId, uint(MAX_CASCADES - 1));
    o.fragUV = tex_coords;
    o.svPosition = mul(mul(lightSpaceMatrices[cascade], pc.model), float4(pos, 1.0));
    return o;
}

[shader("fragment")]
void shadow_fs_main(ShadowVsOut In, uint primitiveID : SV_PrimitiveID)
{
    ObjectDescription obj = fetch_object_description(pc.objectIndex);
    ObjMaterial material = fetch_material(obj, primitiveID);

    // MASK materials alpha-test; OPAQUE/BLEND (alphaCutoff < 0) cast unchanged.
    // The outer guard skips the branch entirely for the common non-MASK case.
    if (material.alphaCutoff >= 0.0)
    {
        if (material.textureID >= 0)
        {
            int textureId = resolve_texture_slot(obj, material);
            float alpha = textures[textureId].Sample(textureSamplers[textureId], transform_uv(In.fragUV, material)).a;
            if (alpha_masked_out(material, alpha)) discard;
        }
        else
        {
            if (alpha_masked_out(material, 1.0)) discard;
        }
    }
}