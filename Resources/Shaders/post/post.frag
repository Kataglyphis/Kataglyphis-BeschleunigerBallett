#version 460

#extension GL_GOOGLE_include_directive : enable

#include "renderer/pushConstants/PushConstantPost.hpp"

layout(location = 0) in vec2 outUV;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D noisyTxt;
layout(set = 0, binding = 1) uniform sampler2D cloudTxt;

layout(push_constant) uniform _PushConstantPost {
    PushConstantPost pc_post;
};

void main()
{
  vec2  uv    = outUV;
  float gamma = 1. / 2.2;

  vec3 color = texture(noisyTxt, uv).rgb;
  vec4 cloud = texture(cloudTxt, uv);
  
  // Blend clouds on top
  // cloud.a contains (1.0 - transmittance) and cloud.rgb is pre-multiplied by energy
  // color = cloud + color * transmittance
  color = cloud.rgb + color * (1.0 - cloud.a);

  //reinhardts tonemapping 
  vec3 tonemapped_color = color / (color + vec3(1.f));

  fragColor   = vec4(pow(tonemapped_color, vec3(gamma)),1.0f);

}