#version 460

#extension GL_ARB_shading_language_include : require

#include "/host_device_shared.hpp"
#include "/bindings.hpp"

#include "/Matlib.glsl"

#include "/material.glsl"
#include "/directional_light.glsl"
#include "/point_light.glsl"

#include "/microfacet.glsl"
#include "/disney.glsl"
#include "/frostbite.glsl"
#include "/pbrBook.glsl"
#include "/phong.glsl"
#include "/unreal4.glsl"

#include "/clouds.glsl"

#ifdef KAT_VULKAN
layout(location = 0) in vec2 tex_coords;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D g_position;
layout(binding = 1) uniform sampler2D g_normal;
layout(binding = 2) uniform sampler2D g_albedo;
layout(binding = 3) uniform sampler2D g_material_id;

layout(binding = 4) uniform sampler2DArray directional_shadow_maps;
layout(binding = 5) uniform sampler3D noise_texture_1;
layout(binding = 6) uniform sampler3D noise_texture_2;
layout(binding = 7) uniform sampler2D random_number;
layout(binding = 8) uniform samplerCube omni_shadow_maps_samplers[MAX_POINT_LIGHTS];

layout(binding = 9) uniform LightingData {
    DirectionalLight directional_light;
    float cascade_endpoints[NUM_CASCADES];
    int pcf_radius;
    Clouds cloud;
    PointLight point_lights[MAX_POINT_LIGHTS];
    float omni_shadow_maps_far_plane[MAX_POINT_LIGHTS];
    int point_light_count;
    Material materials[MAX_MATERIALS];
    vec3 eye_position;
    mat4 view;
    mat4 projection;
} lightingData;

#define directional_light (lightingData.directional_light)
#define cascade_endpoints (lightingData.cascade_endpoints)
#define pcf_radius (lightingData.pcf_radius)
#define cloud (lightingData.cloud)
#define point_lights (lightingData.point_lights)
#define point_light_count (lightingData.point_light_count)
#define materials (lightingData.materials)
#define eye_position (lightingData.eye_position)
#define view (lightingData.view)
#define projection (lightingData.projection)

#else
in vec2 tex_coords;

out vec4 color;

//g_buffer data 
uniform sampler2D g_position;
uniform sampler2D g_normal;
uniform sampler2D g_albedo;
uniform sampler2D g_material_id;

//all regarding the sun and its shadows
uniform DirectionalLight    directional_light;
uniform sampler2DArray      directional_shadow_maps;
uniform float               cascade_endpoints[NUM_CASCADES];
uniform int                 pcf_radius;

//sampler for our noise textures
uniform Clouds      cloud;
uniform sampler3D   noise_texture_1;
uniform sampler3D   noise_texture_2;
// a set of random numbers for getting an good offset for raymarching
// with that we accomplish a round "random" shape for our cloud 
// in [0,1]
uniform sampler2D   random_number;

//all other uniforms
uniform PointLight      point_lights[MAX_POINT_LIGHTS];
uniform OmniShadowMap   omni_shadow_maps[MAX_POINT_LIGHTS];
uniform int             point_light_count;

uniform Material    materials[MAX_MATERIALS];

uniform vec3 eye_position;
uniform mat4 view;
uniform mat4 projection;
#endif

layout (std140, binding = UNIFORM_LIGHT_MATRICES_BINDING) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[NUM_CASCADES];
};


vec4 calc_light_by_direction(   Light light, vec3 direction, float shadow_factor,
                                vec4 albedo, vec4 frag_pos, int material_id, vec3 N) {

    vec3 V          = normalize(eye_position - frag_pos.xyz);
    vec3 L          = normalize(-direction);
    vec3 H          = normalize(V+L);
    
    float roughness = 0.5f;

    vec3 color = vec3(0.f);

    int mode = 4;
	switch (mode) {
	case 0: color += evaluteUnreal4PBR(albedo.xyz, N, L, V, roughness, light.color, light.radiance);
		break;
	case 1: color += evaluatePBRBooksPBR(albedo.xyz, N, L, V, roughness, light.color, light.radiance);
		break;
	case 2: color += evaluateDisneysPBR(albedo.xyz, N, L, V, roughness, light.color, light.radiance);
		break;
	case 3: color += evaluatePhong(albedo.xyz, N, L, V, light.color, light.radiance);
		break;		
	case 4: color += evaluateFrostbitePBR(albedo.xyz, N, L, V, roughness, light.color, light.radiance);
		break;
	}

    return (1.f - shadow_factor) * vec4(color,1.0f);

}

vec4 calc_directional_light(vec4 albedo, vec4 frag_pos, int material_id, vec3 N) {
    
    float shadow_factor = calc_directional_shadow_factor(   directional_light, 
                                                            directional_shadow_maps,
                                                            pcf_radius,
                                                            frag_pos, N, view,
                                                            cascade_endpoints, 
                                                            lightSpaceMatrices);

    return calc_light_by_direction( directional_light.base, directional_light.direction, shadow_factor,
                                    albedo, frag_pos, material_id, N);

}

vec4 calc_point_lights(vec4 frag_pos) {

    vec4 total_color = vec4(0.0f,0.0f,0.0f,0.0f);

    for(int i = 0; i < point_light_count; i++) {

#ifdef KAT_VULKAN
        total_color += calc_point_light(frag_pos.xyz, eye_position,
                                        point_lights[i], omni_shadow_maps_samplers[i], lightingData.omni_shadow_maps_far_plane[i]);
#else
        total_color += calc_point_light(frag_pos.xyz, eye_position,
                                        point_lights[i], omni_shadow_maps[i].shadow_map, omni_shadow_maps[i].far_plane);
#endif

    }

    return total_color;
}

bool belongs_to_skybox(int material_id) {
    
    return material_id == SKYBOX_MATERIAL_ID;

}

bool belongs_to_clouds(int material_id) {

    return (material_id == CLOUDS_MATERIAL_ID);

}

void main () {

    vec4 albedo     = texture(g_albedo, tex_coords);
    vec4 frag_pos   = texture(g_position, tex_coords);
    int material_id = int(texture(g_material_id, tex_coords).r);
    vec3 N          = normalize(texture(g_normal, tex_coords).rgb);
    float rnd_numb  = texture(random_number, tex_coords).r;

    if(belongs_to_skybox(material_id) || belongs_to_clouds(material_id)) {
        // here we only hit the skybox/ or clouds; avoid shading!
        color = albedo;
    } else {
        color = calc_directional_light(albedo, frag_pos, material_id, N);
        // final_color += calc_point_lights(frag_pos);
    }

    vec4 frag_pos_clip_space = projection * view * frag_pos;

    if(belongs_to_clouds(material_id)) {
        
        float far_plane = cascade_endpoints[NUM_CASCADES - 1];

        //color = vec4(abs(mod(frag_pos.xyz, 128.f)) / 128.f, 1.0f);
        calc_clouds(projection, view, 
                    frag_pos, eye_position, 
                    cloud,
                    rnd_numb,
                    noise_texture_1, 
                    noise_texture_2, 
                    directional_light,
                    far_plane,
                    color);
    }

    color.xyz = Reinhards_tonemapping(color.xyz);
    color = vec4(gamma_correction(color.xyz),1.0);

}


