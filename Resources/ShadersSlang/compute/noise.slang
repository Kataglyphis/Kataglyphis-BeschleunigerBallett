// 3D noise volume generation compute kernel.
// Sole source for this shader. Emits SPIR-V only.

[vk::binding(0, 0)] RWTexture3D<float4> noiseVolume;

// Hash function for procedural noise.
float3 hash33(float3 p3)
{
    p3 = frac(p3 * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return frac((p3.xxy + p3.yxx) * p3.zyx);
}

// Procedural Worley Noise.
float worley(float3 p, float numCells)
{
    p *= numCells;
    float3 i = floor(p);
    float3 f = frac(p);

    float minDist = 1.0;
    for (int z = -1; z <= 1; z++)
    {
        for (int y = -1; y <= 1; y++)
        {
            for (int x = -1; x <= 1; x++)
            {
                float3 neighbor = float3(float(x), float(y), float(z));
                float3 neighborWorld = fmod(i + neighbor, numCells);
                float3 point = hash33(neighborWorld);
                float3 diff = neighbor + point - f;
                float dist = length(diff);
                minDist = min(minDist, dist);
            }
        }
    }
    return minDist;
}

// Simple 3D value noise.
float hash13(float3 p3)
{
    p3 = frac(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return frac((p3.x + p3.y) * p3.z);
}

float valueNoise(float3 x)
{
    float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash13(i + float3(0, 0, 0)), hash13(i + float3(1, 0, 0)), f.x),
                     lerp(hash13(i + float3(0, 1, 0)), hash13(i + float3(1, 1, 0)), f.x), f.y),
                 lerp(lerp(hash13(i + float3(0, 0, 1)), hash13(i + float3(1, 0, 1)), f.x),
                     lerp(hash13(i + float3(0, 1, 1)), hash13(i + float3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm_value(float3 x, int octaves)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < octaves; ++i)
    {
        v += a * valueNoise(x);
        x = x * 2.0 + float3(100.0);
        a *= 0.5;
    }
    return v;
}

[shader("compute")]
[numthreads(8, 8, 8)]
void noise_main(uint3 tid : SV_DispatchThreadID)
{
    float3 uvw = float3(tid) / 256.0;
    float worleyVal = worley(uvw, 4.0);
    float perlinVal = fbm_value(uvw * 3.0, 4);
    noiseVolume[tid] = float4(worleyVal, perlinVal, 0.0, 1.0);
}