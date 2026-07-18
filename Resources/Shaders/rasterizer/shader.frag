 #version 460																									

//#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

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
#include "scene/ObjMaterial.hpp"
#include "scene/Vertex.hpp"
#include "ObjectDescription.hpp"

layout (location = 0) in vec2 texture_coordinates;
layout (location = 1) in vec3 shading_normal;
layout (location = 2) in vec3 fragment_color;
layout (location = 3) in vec3 worldPosition;

layout (set = 0, binding = sceneUBO_BINDING) uniform _SceneUBO {
	SceneUBO sceneUBO;
};

layout(set = 0, binding = OBJECT_DESCRIPTION_BINDING, scalar) buffer ObjectDescription_ {
    ObjectDescription i[];
} object_description;

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

layout(set = 0, binding = SAMPLER_BINDING) uniform sampler texture_sampler[MAX_TEXTURE_COUNT];
layout(set = 0, binding = TEXTURES_BINDING) uniform texture2D tex[MAX_TEXTURE_COUNT];
layout(set = 0, binding = SHADOW_MAP_BINDING) uniform sampler2DArray directional_shadow_maps;

// Fraction of direct light blocked at this fragment: 0 = fully lit,
// 1 = fully shadowed. Cascade chosen by camera distance; 0..1 depth
// (GLM_FORCE_DEPTH_ZERO_TO_ONE project-wide).
float calc_cascaded_shadow(vec3 world_pos, vec3 N, vec3 L) {
    int cascade_count = int(sceneUBO.numCascades);
    if (cascade_count <= 0) { return 0.0; }

    float frag_distance = length(sceneUBO.cam_pos.xyz - world_pos);
    int cascade_index = cascade_count - 1;
    for (int i = 0; i < cascade_count; i++) {
        if (frag_distance < sceneUBO.cascadeSplits[i]) {
            cascade_index = i;
            break;
        }
    }

    vec4 light_space_pos = sceneUBO.cascadeLightSpaceMatrices[cascade_index] * vec4(world_pos, 1.0);
    vec3 proj = light_space_pos.xyz / light_space_pos.w;
    vec2 shadow_uv = proj.xy * 0.5 + 0.5;
    float current_depth = proj.z;
    if (current_depth > 1.0 || current_depth < 0.0
        || shadow_uv.x < 0.0 || shadow_uv.x > 1.0 || shadow_uv.y < 0.0 || shadow_uv.y > 1.0) {
        return 0.0;
    }

    float bias = max(0.002 * (1.0 - dot(N, L)), 0.0005);
    int radius = int(sceneUBO.pcfRadius);
    vec2 texel = 1.0 / vec2(textureSize(directional_shadow_maps, 0).xy);
    float occluded = 0.0;
    float taps = 0.0;
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            float map_depth = texture(directional_shadow_maps,
                vec3(shadow_uv + vec2(x, y) * texel, float(cascade_index))).r;
            occluded += (current_depth - bias) > map_depth ? 1.0 : 0.0;
            taps += 1.0;
        }
    }
    return occluded / max(taps, 1.0);
}

layout (location = 0) out vec4 out_color;

void main() {
	
	
	ObjectDescription obj_res	= object_description.i[0];						// for now only one object allowed :)
    MaterialIDs materialIDs		= MaterialIDs(obj_res.material_index_address);	// material id per triangle (face)
	Materials materials			= Materials(obj_res.material_address);			// array of all materials

	vec3 L = normalize(vec3(-sceneUBO.dirLight.direction));
	vec3 N = normalize(shading_normal);
	vec3 V = normalize(sceneUBO.cam_pos.xyz - worldPosition);
	
	vec3 ambient = vec3(0.f);

	int texture_id	= materials.m[materialIDs.i[gl_PrimitiveID]].textureID;
	texture_id = clamp(texture_id, 0, MAX_TEXTURE_COUNT - 1);
	ambient			+= texture(sampler2D(tex[texture_id], texture_sampler[texture_id]), texture_coordinates).xyz;
	//ambient			+= materials.m[materialIDs.i[gl_PrimitiveID]].diffuse;

	float roughness = 0.9;
	vec3 light_color = sceneUBO.dirLight.color.rgb;
	float light_intensity = sceneUBO.dirLight.color.w;

	vec3 color = vec3(0);
	// mode : switching between PBR models
	// [0] --> EPIC GAMES 
	// [1] --> PBR BOOK 
	// [2] --> DISNEYS PRINCIPLED
	// [3] --> PHONG
	// [4] --> FROSTBITE
	int mode = 1;
	switch (mode) {
	case 0: color += evaluteUnreal4PBR(ambient, N, L, V, roughness, light_color, light_intensity);
		break;
	case 1: color += evaluatePBRBooksPBR(ambient, N, L, V, roughness, light_color, light_intensity);
		break;
	case 2: color += evaluateDisneysPBR(ambient, N, L, V, roughness, light_color, light_intensity);
		break;
	case 3: color += evaluatePhong(ambient, N, L, V, light_color, light_intensity);
		break;		
	case 4: color += evaluateFrostbitePBR(ambient, N, L, V, roughness, light_color, light_intensity);
		break;
	}

	float shadow = calc_cascaded_shadow(worldPosition, N, L);
	color *= 1.0 - shadow * sceneUBO.cascadedShadowIntensity;

	out_color = vec4(color,1.0);

}
