#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "../common/raycommon.glsl"
#include "../hostDevice/host_device_shared_vars.hpp"
#include "../../../Src/GraphicsEngineVulkan/renderer/SceneUBO.hpp"
#include "../../../Src/GraphicsEngineVulkan/scene/ObjMaterial.hpp"
#include "../../../Src/GraphicsEngineVulkan/scene/Vertex.hpp"
#include "../../../Src/GraphicsEngineVulkan/ObjectDescription.hpp"

layout (location = 0) in vec2 texture_coordinates;
layout (location = 1) in vec3 shading_normal;
layout (location = 2) in vec3 fragment_color;
layout (location = 3) in vec3 worldPosition;

layout(set = 0, binding = OBJECT_DESCRIPTION_BINDING, scalar) buffer ObjectDescription_ {
    ObjectDescription i[];
} object_description;

layout(buffer_reference, scalar) buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) buffer Indices { ivec3 i[]; };
layout(buffer_reference, scalar) buffer MaterialIDs { int i[]; };
layout(buffer_reference, scalar) buffer Materials { ObjMaterial m[]; };

layout(set = 0, binding = SAMPLER_BINDING) uniform sampler texture_sampler[MAX_TEXTURE_COUNT];
layout(set = 0, binding = TEXTURES_BINDING) uniform texture2D tex[MAX_TEXTURE_COUNT];

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outMaterial;

void main() {
    ObjectDescription obj_res = object_description.i[0];
    MaterialIDs materialIDs = MaterialIDs(obj_res.material_index_address);
    Materials materials = Materials(obj_res.material_address);

    int texture_id = materials.m[materialIDs.i[gl_PrimitiveID]].textureID;
    texture_id = clamp(texture_id, 0, MAX_TEXTURE_COUNT - 1);
    vec4 texColor = texture(sampler2D(tex[texture_id], texture_sampler[texture_id]), texture_coordinates);
    if(texColor.a < 0.1) discard;

    outPosition = vec4(worldPosition, 1.0);
    outNormal = vec4(normalize(shading_normal), 1.0);
    outAlbedo = texColor;
    
    // Material defaults for deferred (Roughness, Metallic, AO)
    outMaterial = vec4(0.9, 0.0, 1.0, 1.0); 
}
