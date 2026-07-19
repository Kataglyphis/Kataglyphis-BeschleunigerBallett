#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raycommon.glsl"
#include "host_device_shared_vars.hpp"
#include "SceneUBO.hpp"

#include "unreal4.glsl"
#include "disney.glsl"
#include "pbrBook.glsl"
#include "phong.glsl"
#include "frostbite.glsl"

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inPosition;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inNormal;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inAlbedo;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inMaterial;
layout(input_attachment_index = 4, set = 1, binding = 4) uniform subpassInput inDepth;

layout (set = 0, binding = sceneUBO_BINDING) uniform _SceneUBO {
	SceneUBO sceneUBO;
};
layout(set = 0, binding = SHADOW_MAP_BINDING) uniform sampler2DArray directional_shadow_maps;

#include "cascaded_shadow.glsl"

void main() {
    vec4 position = subpassLoad(inPosition);
    if (position.w == 0.0) {
        discard;
    }

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

    // Apply point lights
    for(uint i = 0; i < sceneUBO.numPointLights; i++) {
        vec3 pL = sceneUBO.pointLights[i].position.xyz - position.xyz;
        float dist = length(pL);
        pL = normalize(pL);
        
        float constant = sceneUBO.pointLights[i].attenuation.x;
        float linear = sceneUBO.pointLights[i].attenuation.y;
        float exp = sceneUBO.pointLights[i].attenuation.z;
        float att = 1.0 / (constant + linear * dist + exp * dist * dist);
        
        color += evaluatePBRBooksPBR(ambient, N, pL, V, roughness, sceneUBO.pointLights[i].color.rgb, sceneUBO.pointLights[i].color.w) * att;
    }

    outColor = vec4(color, 1.0);
}
