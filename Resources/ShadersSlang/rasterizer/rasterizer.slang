import scene_types;
import brdf;
import material_fetch;
import material_rules;
import base_color;
import emission;
import normal_map;
import cascaded_shadow;
import push_constants;
import raster_geometry;

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

[shader("vertex")]
RasterVsOut vs_main(
    float3 positions : POSITION,
    float3 normal : NORMAL,
    float4 color : COLOR,
    float2 tex_coords : TEXCOORD0,
    float4 tangent : TANGENT)
{
    return raster_geometry_vs(globalUBO, pc_raster, positions, normal, color, tex_coords, tangent);
}

[shader("fragment")]
float4 fs_main(RasterVsOut In, uint primitiveID : SV_PrimitiveID, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    ObjectDescription obj = fetch_object_description(pc_raster.objectIndex);
    ObjMaterial material = fetch_material(obj, primitiveID);

    float3 L = normalize(-sceneUBO.dirLight.direction.xyz);
    float3 N = normalize(In.shadingNormal);
    // Back faces reach this shader only where culling was disabled, and
    // culling is disabled only for doubleSided materials (see
    // MeshDrawRecorder.cpp's dynamic eCullMode) - !isFrontFace is therefore
    // the doubleSided test; no ObjMaterial field is needed for it.
    if (!isFrontFace) N = -N;
    if (material.normalTextureID >= 0)
    {
        int normalTexId = resolve_texture_slot(obj, material.normalTextureID);
        float3 normalSample = textures[normalTexId].Sample(textureSamplers[normalTexId], normal_uv(In.texCoords, material)).xyz;
        N = apply_normal_map(N, In.worldTangent, normalSample, material.normalScale);
    }
    float3 V = normalize(sceneUBO.cam_pos.xyz - In.worldPosition);

    // map_d's alpha is independent of which base-colour branch is taken
    // below, so it is sampled once up front and folded into both sides of
    // the textureID branch's alpha_masked_out() call (the glTF alpha product
    // rule: baseColorFactor.a * baseColorTexture.a * COLOR_0.a * map_d).
    float alphaTexSample = 1.0;
    if (material.alphaTextureID >= 0)
    {
        int alphaTexId = resolve_texture_slot(obj, material.alphaTextureID);
        alphaTexSample = textures[alphaTexId].Sample(textureSamplers[alphaTexId], transform_uv(In.texCoords, material)).r;
    }

    float3 albedo;
    if (material.textureID >= 0)
    {
        int textureId = resolve_texture_slot(obj, material);
        float4 baseSample = textures[textureId].Sample(textureSamplers[textureId], transform_uv(In.texCoords, material));
        if (alpha_masked_out(material, baseSample.a * In.fragmentColor.a * alphaTexSample)) discard;
        albedo = base_color(material, baseSample.xyz);
    }
    else
    {
        if (alpha_masked_out(material, In.fragmentColor.a * alphaTexSample)) discard;
        albedo = material.diffuse;
    }

    albedo *= In.fragmentColor.rgb;

    // KHR_materials_unlit: skip lighting, shadowing and emissive alike and
    // return the base colour as-is (spec: "MUST NOT be lit"), before N/V are
    // used for anything.
    if (material.unlit != 0) return float4(albedo, 1.0);

    float metallic = material.metallic;
    float roughness = material_roughness(material);
    if (material.metallicRoughnessTextureID >= 0)
    {
        int metallicRoughnessTexId = resolve_texture_slot(obj, material.metallicRoughnessTextureID);
        float4 mrSample = textures[metallicRoughnessTexId].Sample(textureSamplers[metallicRoughnessTexId], metallic_roughness_uv(In.texCoords, material));
        float2 metallicRoughness = material_metallic_roughness(material, mrSample);
        metallic = metallicRoughness.x;
        roughness = metallicRoughness.y;
    }
    float3 lightColor = sceneUBO.dirLight.color.rgb;
    float lightIntensity = sceneUBO.dirLight.color.w;

    float3 f0 = lerp(float3(0.04), albedo, metallic);
    float3 color = brdf_direct(N, V, L, albedo, metallic, roughness, f0,
                               lightColor * lightIntensity);

    float shadow = calc_cascaded_shadow(In.worldPosition, N, L, sceneUBO);
    color *= 1.0 - shadow * sceneUBO.cascadedShadowIntensity;

    // Emissive is surface-emitted radiance, not reflected light - it is
    // unshadowed and unattenuated, so it is added after shadowing, not before.
    float3 emissiveSample = float3(1.0);
    if (material.emissiveTextureID >= 0)
    {
        int emissiveTexId = resolve_texture_slot(obj, material.emissiveTextureID);
        emissiveSample = textures[emissiveTexId].Sample(textureSamplers[emissiveTexId], emissive_uv(In.texCoords, material)).xyz;
    }
    color += material_emission(material, emissiveSample);

    return float4(color, 1.0);
}