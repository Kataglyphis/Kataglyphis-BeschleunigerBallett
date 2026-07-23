 #version 460																									

//#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"

#include "hostDevice/host_device_shared_vars.hpp"
#include "pushConstants/PushConstantRasterizer.hpp"

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

// object_description + the MaterialIDs/Materials buffer-reference walk (the
// Vertices/Indices refs here were dead) now live in the shared include.
#include "material_fetch.glsl"

layout(set = 0, binding = SAMPLER_BINDING) uniform sampler texture_sampler[MAX_TEXTURE_COUNT];
layout(set = 0, binding = TEXTURES_BINDING) uniform texture2D tex[MAX_TEXTURE_COUNT];
layout(set = 0, binding = SHADOW_MAP_BINDING) uniform sampler2DArray directional_shadow_maps;

#include "cascaded_shadow.glsl"

// Must match shader.vert's declaration exactly - the block is shared across
// stages and GLSL requires identical layout in each.
layout (push_constant) uniform _PushConstantRasterizer {
	PushConstantRasterizer pc_raster;
};

layout (location = 0) out vec4 out_color;

void main() {
	
	
	// Indexed per draw. This read object_description.i[0] unconditionally
	// until 2026-07-20, so every model was shaded with the FIRST model's
	// material and geometry buffer addresses.
	ObjectDescription obj_res = fetch_object_description(pc_raster.objectIndex);

	vec3 L = normalize(vec3(-sceneUBO.dirLight.direction));
	vec3 N = normalize(shading_normal);
	vec3 V = normalize(sceneUBO.cam_pos.xyz - worldPosition);

	ObjMaterial material = fetch_material(obj_res);

	vec3 ambient;
	if (material.textureID >= 0) {
		// textureID is model-LOCAL; texture_offset shifts it into the
		// flattened global array (multi-model scenes).
		int texture_id = clamp(int(obj_res.texture_offset) + material.textureID, 0, MAX_TEXTURE_COUNT - 1);
		vec4 base_sample = texture(sampler2D(tex[texture_id], texture_sampler[texture_id]), transform_uv(texture_coordinates, material));
		// glTF alphaMode MASK: drop fully-cut-out texels so a foliage/decal card
		// casts and shades its silhouette, not the solid quad it is modelled as.
		// alphaCutoff < 0 (OPAQUE/BLEND, and every OBJ material) skips this
		// entirely, so opaque rendering is bit-unchanged.
		if (material.alphaCutoff >= 0.0 && base_sample.a < material.alphaCutoff) {
			discard;
		}
		ambient = base_sample.xyz;
	} else {
		// Untextured material: textureID is -1, and the old clamp-to-0 made
		// it sample whichever texture sat in slot 0 (same defect the PT/RT
		// kernels had). Use the material diffuse.
		ambient = material.diffuse;
	}

	// glTF COLOR_0 vertex colour multiplies the base colour (spec). The vertex
	// shader has always forwarded fragment_color; it was declared here but never
	// read, so vertex-coloured glTF rendered white. Loaders now write (1,1,1)
	// when the mesh carries no colour, making this an identity multiply there.
	ambient *= fragment_color;

	// Blinn-Phong exponent -> roughness (Beckmann mapping). Was a hard-coded
	// 0.9 that nullified the material's shininess entirely; an unset OBJ
	// shininess of 0 maps to fully rough, close to the old constant look.
	float roughness = clamp(sqrt(2.0 / (material.shininess + 2.0)), 0.045, 1.0);
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
