#ifndef P_LIGHT
#define P_LIGHT

#include "./light.glsl"

struct PointLight {

    Light   base;
    vec3    position;
    float   constant;
    float   linear;
    float   exponent;

};

struct OmniShadowMap {

    samplerCube shadow_map;
    float       far_plane;

};

//all sample directions for our omni directional shader
vec3 sample_offset_directions[20] = vec3[](
    vec3(1, 1, 1),  vec3(1, -1, 1),     vec3(-1, -1, 1),    vec3(-1, 1, 1),
    vec3(1, 1, -1), vec3(1, -1, -1),    vec3(-1, -1, -1),   vec3(-1, 1, -1),
    vec3(1, 1, 0),  vec3(1, -1, 0),     vec3(-1, -1, 0),    vec3(-1, 1, 0),
    vec3(1, 0, 1),  vec3(-1, 0, 1),     vec3(1, 0, -1),     vec3(-1, 0, -1),
    vec3(0, 1, 1),  vec3(0, -1, 1),     vec3(0, -1, -1),    vec3(0, 1, -1)
 );

float calc_omni_shadow_factor(  vec3 frag_pos, vec3 eye_position,
                                PointLight p_light, samplerCube shadow_map, float far_plane ) {

    vec3 frag_to_light = frag_pos - p_light.position;
    float current_depth = length(frag_to_light);

    float shadow = 0.0f;
    float bias = 0.05f;
    int num_samples = 20;

    float view_distance = length(eye_position - frag_pos);
    float disk_radius = (1.0f + (view_distance / far_plane)) / 25.0;

    //PCF
    for (int i = 0; i < num_samples; i++) {

        float closest_depth = texture(shadow_map, frag_to_light + sample_offset_directions[i] * disk_radius).r;
        closest_depth *= far_plane;
        if (current_depth - bias > closest_depth) {
            shadow += 1.0f;
        }

    }

    shadow /= float(num_samples);

    return shadow;

}

vec4 calc_point_light(  vec3 frag_pos, vec3 eye_position,
                        PointLight p_light, samplerCube shadow_map, float far_plane) {

    vec3 direction = p_light.position - frag_pos;
    float dist = length(direction);
    direction = normalize(direction);

    float shadow_factor = calc_omni_shadow_factor(  frag_pos, eye_position, 
                                                    p_light, shadow_map, far_plane);

    vec4 color = vec4(p_light.base.color, 1.0f);//calc_light_by_direction(p_light.base, direction, shadow_factor); // 
    //float attentuation =1.0f * dist ;//pow(distance,2) ;
    float attentuation = (p_light.exponent * dist * dist) + (p_light.linear * dist);

    return (color / attentuation);

}

#endif