// glTF normal mapping (Section 3.9.3): perturb the shading normal using a
// tangent-space normal sampled from NORMAL_TEXTURE. `geometricNormal` must
// already carry any doubleSided back-face flip (see
// DoubleSidedBackFacesFlipTheShadingNormal in buildIntegritySuite.cpp) - the
// tangent frame is Gram-Schmidt-orthogonalized against it, so flipping this
// function's *output* instead of its input would leave the tangent/bitangent
// pointing the wrong way. `tangent` is world-space, xyz = direction, w =
// handedness (+-1, glTF convention; see Vertex.hpp). `scale` is glTF
// normalTexture.scale (SS3.9.3): applied to X and Y only, before
// renormalization. Mirrors forward/forward.slang's fs_main (the WebGPU/Rust
// twin).
//
// Deliberately its own module (not folded into material_fetch.slang): the
// ray tracing / path tracing entry points cannot `import material_fetch`
// (duplicate objectDescription binding), but every shading path can import
// this no-binding module - same reasoning as
// base_color.slang/emission.slang/material_rules.slang.
float3 apply_normal_map(float3 geometricNormal, float4 tangent, float3 sampledNormal, float scale = 1.0)
{
    float3 n = normalize(geometricNormal);
    float3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
    float3 b = cross(n, t) * tangent.w;
    float3 nTs = sampledNormal * 2.0 - 1.0;
    nTs.xy *= scale;
    // Row-multiply: mul(v, M), not mul(M, v), against the row-built (t, b, n)
    // - same convention rchit_main and path_tracing's inline hit-shading loop
    // use for their WorldToObject() inverse-transpose.
    return normalize(mul(nTs, float3x3(t, b, n)));
}
