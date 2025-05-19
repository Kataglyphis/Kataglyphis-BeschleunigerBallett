#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"

#include "hostDevice/host_device_shared_vars.hpp"

#include "unreal4.glsl"
#include "disney.glsl"
#include "pbrBook.glsl"
#include "phong.glsl"
#include "frostbite.glsl"

#include "SceneUBO.hpp"
#include "renderer/pushConstants/PushConstantRayTracing.hpp"
#include "scene/ObjMaterial.hpp"
#include "scene/Vertex.hpp"
#include "ObjectDescription.hpp"

hitAttributeEXT vec2 attribs;

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(location = 1) rayPayloadEXT bool isShadowed;

layout (set = 0, binding = sceneUBO_BINDING) uniform _SceneUBO {
    SceneUBO sceneUBO;
};
layout(set = 0, binding = OBJECT_DESCRIPTION_BINDING, scalar) buffer ObjectDescription_ {
    ObjectDescription i[];
} object_description;

layout(set = 0, binding = SAMPLER_BINDING) uniform sampler texture_sampler[MAX_TEXTURE_COUNT];
layout(set = 0, binding = TEXTURES_BINDING) uniform texture2D tex[MAX_TEXTURE_COUNT];

layout(set = 1, binding = TLAS_BINDING) uniform accelerationStructureEXT TLAS;

layout(buffer_reference, scalar) buffer Vertices {
    Vertex v[]; 
}; // Positions of an object

layout(buffer_reference, scalar) buffer Indices {
    ivec3 i[]; 
}; // Triangle indices

layout(buffer_reference, scalar) buffer MaterialIDs {
    int i[]; 
}; // per triangle material id

layout(buffer_reference, scalar) buffer Materials {
	ObjMaterial m[]; 
}; // all materials of .obj

layout(push_constant) uniform _PushConstantRay {
    PushConstantRaytracing pc_ray;
};

void main() {
    
    ObjectDescription obj_res   = object_description.i[gl_InstanceCustomIndexEXT];  // array of all object descriptions
    Indices indices             = Indices(obj_res.index_address);                   // array of all indices
    Vertices vertices           = Vertices(obj_res.vertex_address);                 // array of all vertices
    MaterialIDs materialIDs     = MaterialIDs(obj_res.material_index_address);      // array of per face material indices
    Materials materials		    = Materials(obj_res.material_address);			    // array of all materials
    
    // indices of closest-hit triangle 
    ivec3 ind = indices.i[gl_PrimitiveID];

    // vertex of closest-hit triangle 
    Vertex v0 = vertices.v[ind.x];
    Vertex v1 = vertices.v[ind.y];
    Vertex v2 = vertices.v[ind.z];

    const vec3 barycentrics = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

    // compte coordinate of hit position 
    const vec3 hit_pos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    const vec3 world_hit_pos = vec3(gl_ObjectToWorldEXT * vec4(hit_pos, 1.0f));

    //compute normal at hit position 
    const vec3 normal_hit = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    const vec3 world_normal_hit = normalize(vec3(gl_ObjectToWorldEXT * vec4(normal_hit,1.0f)));

    vec2 texture_coordinates =  v0.texture_coords * barycentrics.x +
                                v1.texture_coords * barycentrics.y +
                                v2.texture_coords * barycentrics.z;

    // material id is stored per primitive
    vec3 ambient = vec3(0.f);
    int texture_id = materials.m[materialIDs.i[gl_PrimitiveID]].textureID;
    ambient += texture(sampler2D(tex[texture_id], texture_sampler[texture_id]), texture_coordinates).xyz;
    //ambient += materials.m[materialIDs.i[gl_PrimitiveID]].diffuse;

    vec3 L = normalize(vec3(-sceneUBO.light_dir)); 
    // no need to normalize
	vec3 N = normalize(normal_hit);
	vec3 V = normalize(sceneUBO.cam_pos.xyz - hit_pos);

    isShadowed = true;
    if(dot(world_normal_hit, L) > 0) {
    
        float t_min = 0.001;
        float t_max = 10000;
        vec3 origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
        vec3 ray_dir = L;
        uint flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
        traceRayEXT(TLAS,  // acceleration structure
                    flags,       // rayFlags
                    0xFF,        // cullMask
                    0,           // sbtRecordOffset
                    0,           // sbtRecordStride
                    1,           // missIndex
                    origin,      // ray origin
                    t_min,        // ray min range
                    ray_dir,      // ray direction
                    t_max,        // ray max range
                    1            // payload (location = 1)
        );

    }

    float roughness = 0.9;
    vec3 light_color = vec3(1.f);
    float light_intensity = 1.f;

	payload.hit_value = ambient;
	// mode : switching between PBR models
	// [0] --> EPIC GAMES 
	// [1] --> PBR BOOK 
	// [2] --> DISNEYS PRINCIPLED 
    // [3] --> PHONG
    // [4] --> FROSTBITE
    if(!isShadowed) {
	    int mode = 4;
	    switch (mode) {
	    case 0: payload.hit_value += evaluteUnreal4PBR(ambient, N, L, V, roughness, light_color, light_intensity);
		    break;
	    case 1: payload.hit_value += evaluatePBRBooksPBR(ambient, N, L, V, roughness, light_color, light_intensity);
		    break;
	    case 2: payload.hit_value += evaluateDisneysPBR(ambient, N, L, V, roughness, light_color, light_intensity);
		    break;
        case 3: payload.hit_value += evaluatePhong(ambient, N, L, V, light_color, light_intensity);
	        break;
        case 4: payload.hit_value += evaluateFrostbitePBR(ambient, N, L, V, roughness, light_color, light_intensity);
	        break;
	    }
    }


}