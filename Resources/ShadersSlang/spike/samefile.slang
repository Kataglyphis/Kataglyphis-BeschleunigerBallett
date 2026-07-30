RWStructuredBuffer<float> sink;
float3 aces_tonemap(float3 x) { return clamp(x, float3(0.0), float3(1.0)); }
[shader("compute")][numthreads(1,1,1)]
void emitMain(uint3 tid : SV_DispatchThreadID) { sink[tid.x] = aces_tonemap(float3(float(tid.x))).x; }
