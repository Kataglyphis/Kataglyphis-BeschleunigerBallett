// Raster push constant, shared between the forward and deferred rasterizers
// (imported rather than hand-duplicated, matching Src/GraphicsEngineVulkan/
// renderer/pushConstants/PushConstantRasterizer.hpp). The C++ struct and this
// one are still two separate, hand-mirrored copies - the C++ side has no
// visibility into Slang - so a layout change here still needs a matching
// change there; this module removes the second Slang copy that used to drift
// with the first.
//
// invModelRows: inverse-transpose of the model matrix, precomputed on the
// CPU (Slang has no inverse() for the SPIR-V target), rows only. Only the
// upper 3x3 is ever read (it transforms a direction, not a point) - a full
// float4x4 here would blow the 128-byte guaranteed push-constant budget
// (model 64 + a full invModel 64 + objectIndex = 132 > 128).
struct PushConstantRasterizer
{
    float4x4 model;
    float4 invModelRows[3];
    uint objectIndex;
};

// Reconstructs the transformed normal from the row-packed inverse-transpose,
// equivalent to mul(fullInvTransposeModel, float4(normal, 0.0)).xyz.
float3 transform_normal(PushConstantRasterizer pc, float3 normal)
{
    return float3(
      dot(pc.invModelRows[0].xyz, normal), dot(pc.invModelRows[1].xyz, normal), dot(pc.invModelRows[2].xyz, normal));
}
