import scene_types;
import rt_types;

// Ray tracing miss shader: background color when a ray hits nothing.
// Mirrors Resources/Shaders/raytracing/raytrace.rmiss. Emits SPIR-V only
// (Vulkan ray tracing — WebGPU has no RT pipeline).

[[vk::push_constant]] PushConstantRaytracing pc_ray;

[shader("miss")]
void rmiss_main(inout HitPayload payload)
{
    payload.hit_value = pc_ray.clear_color.xyz * 0.4;
    payload.is_hit = 0.0;
}