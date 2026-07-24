#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raycommon.glsl"
#include "host_device_shared_vars.hpp"
#include "SceneUBO.hpp"
#include "GlobalUBO.hpp"

#include "unreal4.glsl"
#include "disney.glsl"
#include "pbrBook.glsl"
#include "phong.glsl"
#include "frostbite.glsl"

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inNormal;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inAlbedo;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inMaterial;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inDepth;

layout (set = 0, binding = sceneUBO_BINDING) uniform _SceneUBO {
	SceneUBO sceneUBO;
};
layout (set = 0, binding = globalUBO_BINDING) uniform _GlobalUBO {
	GlobalUBO globalUBO;
};
layout(set = 0, binding = SHADOW_MAP_BINDING) uniform sampler2DArray directional_shadow_maps;

#include "cascaded_shadow.glsl"

void main() {
    // World position reconstructed from depth - the G-buffer used to burn a
    // full rgba16f target on a value the depth attachment already encodes.
    // Background: a cleared depth of 1.0 means no geometry was rasterized.
    const float depth = subpassLoad(inDepth).r;
    if (depth >= 1.0) {
        discard;
    }
    // inUV is the fullscreen triangle's uv; uv*2-1 is exactly the NDC the
    // geometry pass rasterized with (the Y-flip is baked into projection, so
    // no additional flip belongs here). Depth is [0,1] (ZERO_TO_ONE).
    const vec4 clip_pos = vec4(inUV * 2.0 - 1.0, depth, 1.0);
    vec4 world_pos = globalUBO.inv_view * (globalUBO.inv_projection * clip_pos);
    world_pos /= world_pos.w;
    const vec4 position = vec4(world_pos.xyz, 1.0);

    vec4 normal = subpassLoad(inNormal);
    vec4 albedo = subpassLoad(inAlbedo);
    vec4 material = subpassLoad(inMaterial);

    vec3 N = normalize(normal.xyz);
    vec3 L = normalize(vec3(-sceneUBO.dirLight.direction));
    vec3 V = normalize(sceneUBO.cam_pos.xyz - position.xyz);
    
    vec3 ambient = albedo.rgb;
    float roughness = material.r;
    vec3 light_color = sceneUBO.dirLight.color.rgb;
    float light_intensity = sceneUBO.dirLight.color.w;

    vec3 color = evaluatePBRBooksPBR(ambient, N, L, V, roughness, light_color, light_intensity);
    float shadow = calc_cascaded_shadow(position.xyz, N, L);
    color *= 1.0 - shadow * sceneUBO.cascadedShadowIntensity;

    outColor = vec4(color, 1.0);
}
