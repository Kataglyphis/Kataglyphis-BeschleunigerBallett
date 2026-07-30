import brdf;

// Test entry point: forces emit of the BRDF math so we can verify it
// compiles to both SPIR-V and WGSL. Not loaded by any renderer — it exists
// to guard the shared math module in CI (like the Rust tests/shader_export.rs
// guards the naga path). See Scripts/Windows/compile-slang-shaders.ps1.

RWStructuredBuffer<float4> outBuf;

[shader("compute")]
[numthreads(1, 1, 1)]
void brdf_test_main(uint3 tid : SV_DispatchThreadID)
{
    float3 n = float3(0.0, 1.0, 0.0);
    float3 v = float3(0.0, 1.0, 0.0);
    float3 l = normalize(float3(1.0, 1.0, 0.0));
    float3 albedo = float3(0.8, 0.2, 0.1);
    float3 f0 = lerp(float3(0.04), albedo, 0.0);
    float3 result = brdf_direct(n, v, l, albedo, 0.0, 0.5, f0, float3(1.0));
    outBuf[tid.x] = float4(result, 1.0);
}