#version 450
layout(row_major) uniform;
layout(row_major) buffer;

#line 1 0
layout(std430, binding = 0) buffer StructuredBuffer_float_t_0 {
    float _data[];
} sink_0;

#line 2
vec3 aces_tonemap_0(vec3 x_0)
{

#line 2
    return clamp(x_0, vec3(0.0), vec3(1.0));
}


#line 4
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main()
{

#line 4
    sink_0._data[uint(gl_GlobalInvocationID.x)] = aces_tonemap_0(vec3(float(gl_GlobalInvocationID.x))).x;

#line 4
    return;
}

