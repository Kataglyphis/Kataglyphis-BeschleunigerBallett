// Phase 0 spike: trivial Slang shader to prove slangc emits BOTH SPIR-V
// (Vulkan/C++) and WGSL (Rust/WebGPU) from one source. Not a real shader —
// deleted once the toolchain is proven. See docs/shader-sharing.md.

RWStructuredBuffer<float> outputBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    outputBuffer[tid.x] = float(tid.x) * 2.0;
}