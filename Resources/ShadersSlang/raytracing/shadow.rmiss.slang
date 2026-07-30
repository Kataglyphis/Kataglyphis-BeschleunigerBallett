// Ray tracing shadow miss shader: a shadow ray that misses all geometry
// means the fragment is NOT in shadow. Mirrors
// Resources/Shaders/raytracing/shadow.rmiss. Emits SPIR-V only.

[shader("miss")]
void shadow_rmiss_main(inout bool shadowed)
{
    shadowed = false;
}