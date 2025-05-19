#version 460

#extension GL_GOOGLE_include_directive : enable

#include "renderer/pushConstants/PushConstantPost.hpp"

layout(location = 0) in vec2 outUV;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D noisyTxt;

layout(push_constant) uniform _PushConstantPost {
    PushConstantPost pc_post;
};

void main()
{
  vec2  uv    = outUV;
  float gamma = 1. / 2.2;

  vec3 color = texture(noisyTxt, uv).rgb;
  //reinhardts tonemapping 
  vec3 tonemapped_color = color / (color + vec3(1.f));

  fragColor   = vec4(pow(tonemapped_color, vec3(gamma)),1.0f);

}