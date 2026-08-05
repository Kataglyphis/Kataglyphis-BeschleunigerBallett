import scene_types;
import material_fetch;
import material_rules;
import base_color;
import emission;
import normal_map;
import brdf;
import cascaded_shadow;
import push_constants;
import raster_geometry;

// Deferred geometry pass + lighting pass. Sole source for this shader.
// Emits SPIR-V only. Writes G-buffer (normal, albedo, material) for the
// deferred lighting pass.

[[vk::push_constant]] PushConstantRasterizer pc_raster;

[vk::binding(globalUBO_BINDING, 0)] ConstantBuffer<GlobalUBO> globalUBO;
[vk::binding(TEXTURES_BINDING, 0)] Texture2D<float4> textures[MAX_TEXTURE_COUNT];
[vk::binding(SAMPLER_BINDING, 0)] SamplerState textureSamplers[MAX_TEXTURE_COUNT];

struct GBufferOut
{
    float4 outNormal : SV_Target0;
    float4 outAlbedo : SV_Target1;
    float4 outMaterial : SV_Target2;
};

[shader("vertex")]
RasterVsOut geometry_vs_main(
    float3 positions : POSITION,
    float3 normal : NORMAL,
    float4 color : COLOR,
    float2 tex_coords : TEXCOORD0,
    float4 tangent : TANGENT)
{
    return raster_geometry_vs(globalUBO, pc_raster, positions, normal, color, tex_coords, tangent);
}

[shader("fragment")]
GBufferOut geometry_fs_main(RasterVsOut In, uint primitiveID : SV_PrimitiveID, bool isFrontFace : SV_IsFrontFace)
{
    ObjectDescription obj = fetch_object_description(pc_raster.objectIndex);
    ObjMaterial material = fetch_material(obj, primitiveID);

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

    float4 texColor;
    if (material.textureID >= 0)
    {
        int textureId = resolve_texture_slot(obj, material);
        texColor = textures[textureId].Sample(textureSamplers[textureId], transform_uv(In.texCoords, material));
        if (alpha_masked_out(material, texColor.a * In.fragmentColor.a * alphaTexSample)) discard;
        texColor = float4(base_color(material, texColor.rgb), texColor.a);
    }
    else
    {
        if (alpha_masked_out(material, In.fragmentColor.a * alphaTexSample)) discard;
        texColor = float4(material.diffuse, 1.0);
    }

    texColor.rgb *= In.fragmentColor.rgb;

    GBufferOut g;
    // Back faces reach this shader only where culling was disabled, and
    // culling is disabled only for doubleSided materials (see
    // MeshDrawRecorder.cpp's dynamic eCullMode) - !isFrontFace is therefore
    // the doubleSided test. The flip must happen before this G-buffer write,
    // not in the lighting pass, since the lighting pass has no SV_IsFrontFace.
    float3 N = normalize(In.shadingNormal);
    if (!isFrontFace) N = -N;
    if (material.normalTextureID >= 0)
    {
        int normalTexId = resolve_texture_slot(obj, material.normalTextureID);
        float3 normalSample = textures[normalTexId].Sample(textureSamplers[normalTexId], normal_uv(In.texCoords, material)).xyz;
        N = apply_normal_map(N, In.worldTangent, normalSample, material.normalScale);
    }
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
    // .w is otherwise unused (GBUFFER_NORMAL_FORMAT is eR16G16B16A16Sfloat,
    // and lighting_fs_main only ever read normal.xyz) - the free float
    // channel for metallic, so it does not need its own G-buffer attachment.
    g.outNormal = float4(N, metallic);
    // GBUFFER_ALBEDO_FORMAT is eR8G8B8A8Srgb: base colour is linear by
    // construction (glTF caps baseColorFactor/COLOR_0 to [0,1], texColor is
    // decoded from an sRGB source texture), so the hardware re-encodes on
    // write and decodes on SubpassLoad below - the stored value is
    // unchanged in linear terms, only its 256 steps are redistributed
    // toward the range the eye is most sensitive to.
    //
    // .a carries KHR_materials_unlit instead of alpha (texColor.a is always
    // 1.0 by the time it reaches here - BLEND is not implemented and MASK
    // fragments below the cutoff already discarded above), because the
    // lighting pass runs in a separate subpass and cannot see ObjMaterial at
    // all - the G-buffer is its only channel for the flag. Vulkan applies the
    // sRGB transfer function to RGB only, so alpha round-trips linearly
    // through the store and SubpassLoad below, making 0.0/1.0 exact. Grepped
    // Src/ for other readers of the albedo attachment: FrameCapture and the
    // golden suites both only read colour attachments by index for pixel
    // comparison and do not interpret alpha, so repurposing it here is safe.
    g.outAlbedo = float4(texColor.rgb, material.unlit != 0 ? 1.0 : 0.0);
    // Emissive is surface-emitted radiance: the base-colour*emissiveTexture
    // multiply happens here, in the geometry pass, since the lighting pass
    // only has the already-packed G-buffer value to work with (no material
    // or texCoords).
    float3 emissiveSample = float3(1.0);
    if (material.emissiveTextureID >= 0)
    {
        int emissiveTexId = resolve_texture_slot(obj, material.emissiveTextureID);
        emissiveSample = textures[emissiveTexId].Sample(textureSamplers[emissiveTexId], emissive_uv(In.texCoords, material)).xyz;
    }
    // r = roughness, gba = emissive. GBUFFER_MATERIAL_FORMAT is
    // eR16G16B16A16Sfloat, so emissive above 1.0 from
    // KHR_materials_emissive_strength survives the G-buffer round-trip
    // instead of clamping to [0,1], and roughness is no longer quantized
    // to 256 steps.
    g.outMaterial = float4(roughness, material_emission(material, emissiveSample));
    return g;
}

// --- Deferred lighting pass ---

[vk::binding(globalUBO_BINDING, 0)] ConstantBuffer<GlobalUBO> globalUBO_lighting;
[vk::binding(sceneUBO_BINDING, 0)] ConstantBuffer<SceneUBO> sceneUBO_lighting;

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

    // KHR_materials_unlit, carried through geometry_fs_main's outAlbedo.a
    // (see its write site for why alpha is the free channel): skip lighting,
    // shadowing and emissive alike and return the base colour as-is.
    if (albedo.a > 0.5) return float4(albedo.rgb, 1.0);

    float3 N = normalize(normal.xyz);
    float3 L = normalize(-sceneUBO_lighting.dirLight.direction.xyz);
    float3 V = normalize(sceneUBO_lighting.cam_pos.xyz - worldPos.xyz);

    float roughness = material.r;
    float metallic = normal.w;
    float3 lightColor = sceneUBO_lighting.dirLight.color.rgb;
    float lightIntensity = sceneUBO_lighting.dirLight.color.w;

    float3 f0 = lerp(float3(0.04), albedo.rgb, metallic);
    float3 color = brdf_direct(N, V, L, albedo.rgb, metallic, roughness, f0,
                               lightColor * lightIntensity);

    float shadow = calc_cascaded_shadow(worldPos.xyz, N, L, sceneUBO_lighting);
    color *= 1.0 - shadow * sceneUBO_lighting.cascadedShadowIntensity;

    // Emissive is surface-emitted radiance, not reflected light - it is
    // unshadowed and unattenuated, so it is added after shadowing, not before.
    color += material.gba;

    return float4(color, 1.0);
}