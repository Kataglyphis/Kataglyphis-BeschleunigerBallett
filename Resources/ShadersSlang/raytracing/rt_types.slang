// Ray tracing-specific types: the RT-specific payload and push constant
// types not covered by scene_types.slang.

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