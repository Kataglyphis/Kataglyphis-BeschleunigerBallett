#version 460

#ifdef KAT_VULKAN
layout(location = 0) in vec4 frag_pos;
layout(push_constant) uniform PushConstants {
	vec3 light_pos;
	float far_plane;
};
#else
in vec4 frag_pos;
uniform vec3 light_pos;
uniform float far_plane;
#endif

void main() {

	float distance = length(frag_pos.xyz - light_pos); 
	distance = distance/far_plane;
	gl_FragDepth = distance;

}