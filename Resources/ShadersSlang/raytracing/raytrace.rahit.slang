import scene_types;
import rt_types;
import material_fetch;
import alpha_test;

// Ray tracing any-hit shader. Sole source for this shader. Emits SPIR-V
// only.
//
// Alpha-tests glTF MASK materials via alpha_test.slang's ray_hit_masked_out(),
// shared with path_tracing.slang's ray-query candidate loop (which has no
// any-hit stage of its own). Shared by both the primary ray's hit group and
// the shadow ray (raytrace.rchit.slang's shadow TraceRay), so it must never
// write `payload` - the shadow ray's payload is a `bool`, not a
// `HitPayload`.

[shader("anyhit")]
void rahit_main(
    inout HitPayload payload,
    in BuiltInTriangleIntersectionAttributes attribs)
{
    // Per-mesh object description: same lookup as rchit_main.
    uint objIndex = InstanceIndex() + GeometryIndex();
    ObjectDescription obj = fetch_object_description(objIndex);

    if (ray_hit_masked_out(obj, PrimitiveIndex(), attribs.barycentrics))
    {
        IgnoreHit();
    }
}
