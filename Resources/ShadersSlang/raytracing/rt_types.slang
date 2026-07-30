// Ray tracing-specific types. Imports the shared scene types from
// scene_types.slang (which has NO ray tracing capabilities) and adds the
// RT-specific payload and push constant types.

import scene_types;

// Ray payload carried between rgen -> rchit/rmiss.
struct HitPayload
{
    float3 hit_value;
    float is_hit;
};

// Push constants for the ray tracing pass.
struct PushConstantRaytracing
{
    float4 clear_color;
};