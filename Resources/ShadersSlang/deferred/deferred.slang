import scene_types;
import material_fetch;
import base_color;
import brdf;
import cascaded_shadow;
import push_constants;

// Deferred geometry pass + lighting pass. Sole source for this shader.
// Emits SPIR-V only. Writes G-buffer (normal, albedo, material) for the
// deferred lighting pass.

[[vk::push_constant]] PushConstantRasterizer pc_raster;

[vk::binding(0, 0)] ConstantBuffer<GlobalUBO> globalUBO;
[vk::binding(3, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];
[vk::binding(4, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];

struct GVsOut
{
    float4 svPosition : SV_Position;
    float2 texCoords : TEXCOORD0;
    float3 shadingNormal : TEXCOORD1;
    float4 fragmentColor : TEXCOORD2;
    float3 worldPosition : TEXCOORD3;
};

struct GBufferOut
{
    float4 outNormal : SV_Target0;
    float4 outAlbedo : SV_Target1;
    float4 outMaterial : SV_Target2;
};

[shader("vertex")]
GVsOut geometry_vs_main(
    float3 positions : POSITION,
    float3 normal : NORMAL,
    float4 color : COLOR,
    float2 tex_coords : TEXCOORD0)
{
    GVsOut o;
    o.svPosition = mul(mul(mul(globalUBO.projection, globalUBO.view), pc_raster.model), float4(positions, 1.0));
    o.worldPosition = mul(pc_raster.model, float4(positions, 1.0)).xyz;
    o.shadingNormal = transform_normal(pc_raster, normal);
    o.texCoords = tex_coords;
    o.fragmentColor = color;
    return o;
}

[shader("fragment")]
GBufferOut geometry_fs_main(GVsOut In, uint primitiveID : SV_PrimitiveID)
{
    ObjectDescription obj = fetch_object_description(pc_raster.objectIndex);
    ObjMaterial material = fetch_material(obj, primitiveID);

    float4 texColor;
    if (material.textureID >= 0)
    {
        int textureId = resolve_texture_slot(obj, material);
        texColor = textures[textureId].Sample(textureSamplers[textureId], transform_uv(In.texCoords, material));
        if (alpha_masked_out(material, texColor.a * In.fragmentColor.a)) discard;
        texColor = float4(base_color(material, texColor.rgb), texColor.a);
    }
    else
    {
        if (alpha_masked_out(material, In.fragmentColor.a)) discard;
        texColor = float4(material.diffuse, 1.0);
    }

    texColor.rgb *= In.fragmentColor.rgb;

    GBufferOut g;
    g.outNormal = float4(normalize(In.shadingNormal), 1.0);
    g.outAlbedo = texColor;
    g.outMaterial = float4(clamp(sqrt(2.0 / (material.shininess + 2.0)), 0.045, 1.0), 0.0, 1.0, 1.0);
    return g;
}

// --- Deferred lighting pass ---

[vk::binding(0, 0)] ConstantBuffer<GlobalUBO> globalUBO_lighting;
[vk::binding(1, 0)] ConstantBuffer<SceneUBO> sceneUBO_lighting;

[[vk::binding(0, 1)]] [[vk::input_attachment_index(0)]] SubpassInput<float4> inNormal;
[[vk::binding(1, 1)]] [[vk::input_attachment_index(1)]] SubpassInput<float4> inAlbedo;
[[vk::binding(2, 1)]] [[vk::input_attachment_index(2)]] SubpassInput<float4> inMaterial;
[[vk::binding(3, 1)]] [[vk::input_attachment_index(3)]] SubpassInput<float4> inDepth;

struct LightingVsOut
{
    float4 svPosition : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
LightingVsOut lighting_vs_main(uint vid : SV_VertexID)
{
    LightingVsOut o;
    float x = float(vid / 2) * 4.0 - 1.0;
    float y = float(vid % 2) * 4.0 - 1.0;
    o.svPosition = float4(x, y, 0.0, 1.0);
    o.uv = float2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
    return o;
}

[shader("fragment")]
float4 lighting_fs_main(LightingVsOut In) : SV_Target
{
    float depth = inDepth.SubpassLoad().r;
    if (depth >= 1.0) discard;

    float4 clipPos = float4(In.uv * 2.0 - 1.0, depth, 1.0);
    float4 worldPos = mul(globalUBO_lighting.inv_view, mul(globalUBO_lighting.inv_projection, clipPos));
    worldPos /= worldPos.w;

    float4 normal = inNormal.SubpassLoad();
    float4 albedo = inAlbedo.SubpassLoad();
    float4 material = inMaterial.SubpassLoad();

    float3 N = normalize(normal.xyz);
    float3 L = normalize(-sceneUBO_lighting.dirLight.direction.xyz);
    float3 V = normalize(sceneUBO_lighting.cam_pos.xyz - worldPos.xyz);

    float3 ambient = albedo.rgb;
    float roughness = material.r;
    float3 lightColor = sceneUBO_lighting.dirLight.color.rgb;
    float lightIntensity = sceneUBO_lighting.dirLight.color.w;

    float3 f0 = lerp(float3(0.04), ambient, 0.0);
    float3 color = brdf_direct(N, V, L, ambient, 0.0, roughness, f0,
                               lightColor * lightIntensity);

    float shadow = calc_cascaded_shadow(worldPos.xyz, N, L, sceneUBO_lighting);
    color *= 1.0 - shadow * sceneUBO_lighting.cascadedShadowIntensity;

    return float4(color, 1.0);
}