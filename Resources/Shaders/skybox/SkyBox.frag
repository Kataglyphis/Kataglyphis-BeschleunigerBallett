#version 460

#extension GL_GOOGLE_include_directive : enable

#include "host_device_shared.hpp"

layout(location = 0) out vec4 g_albedo;

layout(location = 0) in vec3 in_worldDir;

layout(set = 1, binding = 1) uniform samplerCube skybox;

layout(push_constant) uniform _PushConstantSkyBox {
    uint skybox_enabled;
};

void main() {
    if (bool(skybox_enabled)) {
        g_albedo = vec4(texture(skybox, normalize(vec3(in_worldDir.x, in_worldDir.y, in_worldDir.z))).xyz, 1.0);
    } else {
        g_albedo = vec4(0.0, 0.0, 0.5, 1.0);
    }
}