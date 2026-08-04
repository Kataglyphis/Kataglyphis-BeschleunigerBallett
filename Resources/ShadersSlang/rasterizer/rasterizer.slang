import scene_types;
import brdf;
import material_fetch;
import base_color;
import cascaded_shadow;
import push_constants;

// Forward rasterizer shaders. Sole source for this shader. Emits SPIR-V only.
//
// NOTE: The GLSL vertex shader uses inverse(model) for the normal matrix.
// Slang has no inverse() for SPIR-V, so the normal matrix is precomputed on
// the CPU and passed as row-packed invModelRows in the push constants (see
// common/push_constants.slang for why it is rows, not a full float4x4).

[[vk::push_constant]] PushConstantRasterizer pc_raster;

[vk::binding(0, 0)] ConstantBuffer<GlobalUBO> globalUBO;
[vk::binding(1, 0)] ConstantBuffer<SceneUBO> sceneUBO;
[vk::binding(3, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];
[vk::binding(4, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];

struct VsOut
{
    float4 svPosition : SV_Position;
    float2 texCoords : TEXCOORD0;
    float3 shadingNormal : TEXCOORD1;
    float4 fragmentColor : TEXCOORD2;
    float3 worldPosition : TEXCOORD3;
};

[shader("vertex")]
VsOut vs_main(
    float3 positions : POSITION,
    float3 normal : NORMAL,
    float4 color : COLOR,
    float2 tex_coords : TEXCOORD0)
{
    VsOut o;
    o.svPosition = mul(mul(mul(globalUBO.projection, globalUBO.view), pc_raster.model), float4(positions, 1.0));
    o.worldPosition = mul(pc_raster.model, float4(positions, 1.0)).xyz;
    o.shadingNormal = transform_normal(pc_raster, normal);
    o.texCoords = tex_coords;
    o.fragmentColor = color;
    return o;
}

[shader("fragment")]
float4 fs_main(VsOut In, uint primitiveID : SV_PrimitiveID) : SV_Target
{
    ObjectDescription obj = fetch_object_description(pc_raster.objectIndex);
    ObjMaterial material = fetch_material(obj, primitiveID);

    float3 L = normalize(-sceneUBO.dirLight.direction.xyz);
    float3 N = normalize(In.shadingNormal);
    float3 V = normalize(sceneUBO.cam_pos.xyz - In.worldPosition);

    float3 ambient;
    if (material.textureID >= 0)
    {
        int textureId = resolve_texture_slot(obj, material);
        float4 baseSample = textures[textureId].Sample(textureSamplers[textureId], transform_uv(In.texCoords, material));
        if (alpha_masked_out(material, baseSample.a * In.fragmentColor.a)) discard;
        ambient = base_color(material, baseSample.xyz);
    }
    else
    {
        if (alpha_masked_out(material, In.fragmentColor.a)) discard;
        ambient = material.diffuse;
    }

    ambient *= In.fragmentColor.rgb;

    float roughness = clamp(sqrt(2.0 / (material.shininess + 2.0)), 0.045, 1.0);
    float3 lightColor = sceneUBO.dirLight.color.rgb;
    float lightIntensity = sceneUBO.dirLight.color.w;

    float3 f0 = lerp(float3(0.04), ambient, 0.0);
    float3 color = brdf_direct(N, V, L, ambient, 0.0, roughness, f0,
                               lightColor * lightIntensity);

    float shadow = calc_cascaded_shadow(In.worldPosition, N, L, sceneUBO);
    color *= 1.0 - shadow * sceneUBO.cascadedShadowIntensity;

    return float4(color, 1.0);
}