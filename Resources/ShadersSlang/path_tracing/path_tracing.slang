import scene_types;
import base_color;
import emission;
import normal_map;
import alpha_test;
import material_fetch;
import material_rules;

// Path tracing compute kernel using ray query (GL_EXT_ray_query).
// Sole source for this shader. Emits SPIR-V only (Vulkan ray query — WebGPU
// has no ray query).
//
// Uses Slang T* pointers (PhysicalStorageBuffer) for buffer device
// addresses, RayQuery for ray traversal, next-event estimation toward the
// directional light (Lambertian 1/pi, terminate-on-first-hit shadow rays),
// Russian roulette from the 4th bounce, and RNG (pcg) for stochastic
// sampling and temporal accumulation (running-mean history image).

struct PushConstantPathTracing
{
    float4 clearColor;
    uint width;
    uint height;
    uint frame_index;
    uint samples_per_pixel;
    uint max_bounces;
};

[vk::binding(globalUBO_BINDING, 0)] ConstantBuffer<GlobalUBO> globalUBO;
[vk::binding(sceneUBO_BINDING, 0)] ConstantBuffer<SceneUBO> sceneUBO;
// objectDescription (OBJECT_DESCRIPTION_BINDING) comes from material_fetch.slang.
// textures/textureSamplers (TEXTURES_BINDING, SAMPLER_BINDING) come from
// alpha_test.slang, shared with raytrace.rahit.slang's alpha test.

[vk::binding(TLAS_BINDING, 1)] RaytracingAccelerationStructure TLAS;
[vk::binding(OUT_IMAGE_BINDING, 1)] RWTexture2D<float4> image;
[vk::binding(ACCUMULATION_IMAGE_BINDING, 1)] RWTexture2D<float4> accumulationImage;

[[vk::push_constant]] PushConstantPathTracing pc_ray;

// PCG RNG: condensed pcg_output_rxs_mxs_32_32.
float stepAndOutputRNGFloat(inout uint rngState)
{
    rngState = rngState * 747796405u + 1u;
    uint word = ((rngState >> ((rngState >> 28u) + 4u)) ^ rngState) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word) / 4294967295.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void path_tracing_main(uint3 tid : SV_DispatchThreadID)
{
    uint2 resolution = uint2(pc_ray.width, pc_ray.height);
    if (tid.x >= resolution.x || tid.y >= resolution.y)
        return;

    // RNG state: frame index folded in so every frame draws different samples.
    uint rngState = (resolution.x * tid.y + tid.x) + pc_ray.frame_index * 2654435769u;

    float4 cameraOrigin = mul(globalUBO.inv_view, float4(0, 0, 0, 1));

    int numSamples = int(pc_ray.samples_per_pixel);
    int maxBounces = int(pc_ray.max_bounces);
    float3 summedColor = float3(0.0);
    // Furnace debug mode (KATAGLYPHIS_PT_FURNACE): clearColor.w set forces
    // every hit's albedo to 1 and the environment to a uniform clearColor.rgb
    // radiance, so an unbiased estimator must converge every pixel to exactly
    // that radiance regardless of geometry.
    bool furnace = pc_ray.clearColor.w > 0.5;

    for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++)
    {
        // Jitter within the pixel's unit square (a box filter) instead of
        // always sampling the exact centre: this is what makes
        // samples_per_pixel and the temporal running mean anti-alias
        // geometric edges rather than just denoise a point-sampled image.
        float2 pixelCenter = float2(tid.xy) + float2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState));
        float2 uv = pixelCenter / float2(resolution);
        float2 cs = uv * 2.0 - 1.0;

        float4 target = mul(globalUBO.inv_projection, float4(cs.x, cs.y, 1, 1));
        float3 rayOrigin = cameraOrigin.xyz;
        float3 rayDirection = normalize(mul(globalUBO.inv_view, float4(normalize(target.xyz), 0)).xyz);

        // Throughput: the product of every bounce's albedo (and RR
        // reweighting) applied so far. Radiance: the running estimate this
        // sample contributes, accumulated at every NEE hit and sky miss.
        float3 throughput = float3(1.0);
        float3 radiance = float3(0.0);

        for (int bounce = 0; bounce < maxBounces; bounce++)
        {
            // Russian roulette from the fourth segment on: terminate
            // low-throughput paths with probability (1 - survival) and
            // reweight survivors by 1/survival, keeping the estimator
            // unbiased while skipping bounces that would contribute almost
            // nothing.
            if (bounce >= 3)
            {
                float survival = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
                if (stepAndOutputRNGFloat(rngState) > survival)
                {
                    break;
                }
                throughput /= survival;
            }

            RayQuery<0> rayQuery;
            RayDesc ray;
            ray.Origin = rayOrigin;
            ray.Direction = rayDirection;
            ray.TMin = 0.001;
            ray.TMax = 10000.0;
            rayQuery.TraceRayInline(TLAS, 0, 0xFFu, ray);
            // Proceed() performs one traversal step and must be looped until
            // it returns false (GL_EXT_ray_query: rayQueryProceedEXT) — a
            // single call can leave traversal incomplete, so
            // CommittedStatus() and the committed-hit accessors below would
            // read undefined state.
            //
            // Deliberately not forced opaque: a ray query has no any-hit
            // stage, so glTF MASK cut-outs are alpha-tested here in the
            // candidate loop instead - CommitNonOpaqueTriangleHit() only for
            // candidates that pass, which is what implements the discard.
            while (rayQuery.Proceed())
            {
                if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
                {
                    uint candidateObjIndex = rayQuery.CandidateInstanceIndex() + rayQuery.CandidateGeometryIndex();
                    ObjectDescription candidateObj = fetch_object_description(candidateObjIndex);
                    if (!ray_hit_masked_out(candidateObj, rayQuery.CandidatePrimitiveIndex(), rayQuery.CandidateTriangleBarycentrics()))
                    {
                        rayQuery.CommitNonOpaqueTriangleHit();
                    }
                }
            }

            if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
            {
                // Ray hit the sky. A deliberate soft gradient environment
                // (RTIOW-style) scaled well below the directional light so
                // the NEE term below dominates the look, or the uniform
                // furnace radiance in furnace mode.
                float3 skyRadiance;
                if (furnace)
                {
                    skyRadiance = pc_ray.clearColor.rgb;
                }
                else
                {
                    float skyT = 0.5 * (rayDirection.y + 1.0);
                    skyRadiance = 0.5 * lerp(float3(1.0), float3(0.5, 0.7, 1.0), skyT);
                }
                radiance += throughput * skyRadiance;
                break;
            }

            // KNOWN ISSUE, unresolved (see BACKLOG.md): on the large
            // Dinosaurs.obj scene this hit-info block still reproduces
            // VK_ERROR_DEVICE_LOST on the RX 9070 XT with zero preceding
            // validation errors, whenever v0/v1/v2.position or .normal
            // (fetched via this RayQuery-derived Vertices* BDA pointer)
            // actually feed the output image - regardless of whether a
            // matrix transform is applied, whether the code is factored into
            // a helper or inlined as here, whether sceneUBO is involved, and
            // independent of
            // CommittedObjectToWorld3x4()/CommittedWorldToObject3x4()
            // specifically (removing those calls and using the raw
            // object-space position still faults). Consuming ONLY the
            // material-fetched .color (no vertex-buffer position/normal
            // read) never faults, across 40+ frames and an 8x-per-invocation
            // RayQuery loop, on this same large mesh.
            // raytracing/raytrace.rchit.slang reads the identical fields via
            // the identical Vertex/Vertices BDA types successfully, but has
            // only ever been
            // exercised against the tiny shadow_rig.obj rig - it is NOT yet
            // known whether this is scoped to RayQuery/compute specifically
            // or is a more general large-mesh vertex-buffer-device-address
            // bug the RT pipeline has simply never hit. Root-causing further
            // needs a GPU capture (RenderDoc/PIX) or AMD crash-dump tooling,
            // or an RT-pipeline golden test against the dinosaur mesh - out
            // of reach here.
            uint instanceIndex = rayQuery.CommittedInstanceIndex();
            float3x4 objToWorld = rayQuery.CommittedObjectToWorld3x4();
            float3x4 worldToObject = rayQuery.CommittedWorldToObject3x4();

            ObjectDescription obj = fetch_object_description(instanceIndex);
            Vertices* vertices = (Vertices*)obj.vertex_address;
            Indices* indices = (Indices*)obj.index_address;

            uint primitiveID = rayQuery.CommittedPrimitiveIndex();

            int3 tri = indices->i[primitiveID];
            Vertex v0 = vertices->v[tri.x];
            Vertex v1 = vertices->v[tri.y];
            Vertex v2 = vertices->v[tri.z];

            float2 bary = rayQuery.CommittedTriangleBarycentrics();
            float3 barycentrics = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

            float3 objectPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
            // float3x4 is 3 rows × 4 cols; mul(float3x4, float4) = float3.
            float3 hitWorldPosition = mul(objToWorld, float4(objectPos, 1.0));

            float3 normalHit = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
            // Inverse-transpose = row-multiply worldToObject (mul(v, M), not
            // mul(M, v)); see raytrace.rchit.slang for the same rule.
            float3 hitWorldNormal = normalize(mul(normalHit, (float3x3)worldToObject));
            // Flip the normal so it points against the ray direction
            // (GLSL faceforward(N, I, N)).
            if (dot(hitWorldNormal, rayDirection) > 0.0)
            {
                hitWorldNormal = -hitWorldNormal;
            }

            float2 texCoords = v0.texture_coords * barycentrics.x
                             + v1.texture_coords * barycentrics.y
                             + v2.texture_coords * barycentrics.z;

            ObjMaterial material = fetch_material(obj, primitiveID);

            // Shading normal for the NEE lighting term below (nDotL): the
            // geometric hitWorldNormal itself stays unperturbed, since it also
            // drives the ray-offset (self-intersection guard) and the bounce
            // hemisphere sampling, and perturbing those would couple the
            // normal map into path geometry rather than just shading.
            float3 shadingNormal = hitWorldNormal;
            if (material.normalTextureID >= 0)
            {
                float4 tangentHit = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;
                // float3x4 is 3 rows x 4 cols; mul(float3x4, float4) = float3.
                // Same "surface direction, not a normal" rule as
                // raytracing/raytrace.rchit.slang - transforms with objToWorld
                // directly, not the inverse-transpose worldToObject the normal
                // above uses.
                float3 worldTangent = normalize(mul(objToWorld, float4(tangentHit.xyz, 0.0)));
                int normalTexId = resolve_texture_slot(obj, material.normalTextureID);
                // Explicit LOD: see the base-colour sample below for why.
                float3 normalSample = textures[normalTexId].SampleLevel(textureSamplers[normalTexId], normal_uv(texCoords, material), 0.0).xyz;
                shadingNormal = apply_normal_map(hitWorldNormal, float4(worldTangent, tangentHit.w), normalSample, material.normalScale);
            }

            float3 hitColor;
            if (furnace)
            {
                // Furnace mode must keep forcing albedo to exactly 1, or
                // PathTracingPassesTheWhiteFurnaceTest's "converges to
                // exactly the environment radiance" invariant breaks - so
                // the vertex-colour multiply below is skipped here.
                hitColor = float3(1.0);
            }
            else
            {
                if (material.textureID >= 0)
                {
                    int textureId = resolve_texture_slot(obj, material);
                    // Explicit LOD: this compute kernel has no fragment
                    // derivatives, so an implicit-LOD .Sample() fails SPIR-V
                    // validation. See rchit_main's base-colour sample comment
                    // in raytracing/raytrace.rchit.slang.
                    hitColor = base_color(material, textures[textureId].SampleLevel(textureSamplers[textureId], transform_uv(texCoords, material), 0.0).xyz);
                }
                else
                {
                    hitColor = material.diffuse;
                }

                float4 vertexColor = v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z;
                hitColor *= vertexColor.rgb;

                // glTF metallic-roughness texture: this Lambertian-only path
                // tracer has no specular BRDF and, unlike the other three
                // shading paths, never consumed ObjMaterial::metallic at all -
                // so there is no "existing metallic local" here to feed. Gated
                // strictly on the texture (not the metallic FACTOR alone) to
                // keep every pre-existing scene bit-unchanged, matching the
                // other three shading paths' "no MR texture -> today's
                // factors" contract. Uses common/material_rules.slang's
                // material_metallic_roughness() - the same binding-free
                // module the other three shading paths call - so this kernel
                // no longer has to duplicate the B-channel = metallic
                // swizzle; only the metallic half (.x) is used, since this
                // path has no roughness term.
                if (material.metallicRoughnessTextureID >= 0)
                {
                    int metallicRoughnessTexId = resolve_texture_slot(obj, material.metallicRoughnessTextureID);
                    // Explicit LOD: see the base-colour sample above for why.
                    float4 mrSample = textures[metallicRoughnessTexId].SampleLevel(textureSamplers[metallicRoughnessTexId], metallic_roughness_uv(texCoords, material), 0.0);
                    hitColor *= (1.0 - material_metallic_roughness(material, mrSample).x);
                }
            }

            // KHR_materials_unlit: the raster reading is "return base colour
            // before any lighting" - the path-tracing equivalent is to skip
            // NEE and hemisphere scattering entirely and terminate the path
            // here, contributing only the (throughput-attenuated) base
            // colour. Furnace mode stays gated out, same reason as emissive
            // below: hitColor is already forced to 1.0 there and an early
            // terminator would change how many bounces contribute, breaking
            // the "converges to exactly the environment radiance" invariant.
            if (!furnace && material.unlit != 0)
            {
                radiance += throughput * hitColor;
                break;
            }

            // Emissive is surface-emitted radiance, added at the hit before
            // throughput absorbs this bounce's albedo - furnace mode stays
            // gated out, or a furnace emitter would break the "converges to
            // exactly the environment radiance" invariant.
            if (!furnace)
            {
                float3 emissiveSample = float3(1.0);
                if (material.emissiveTextureID >= 0)
                {
                    int emissiveTexId = resolve_texture_slot(obj, material.emissiveTextureID);
                    // Explicit LOD: see the base-colour sample above for why.
                    emissiveSample = textures[emissiveTexId].SampleLevel(textureSamplers[emissiveTexId], emissive_uv(texCoords, material), 0.0).xyz;
                }
                radiance += throughput * material_emission(material, emissiveSample);
            }

            // Apply color absorption.
            throughput *= hitColor;

            // Start the next ray at the hit position, offset slightly along
            // the normal to guard against self-intersection.
            rayOrigin = hitWorldPosition + 0.0001 * hitWorldNormal;

            // Next-event estimation toward the directional light: one
            // shadow ray per bounce. The light is a delta direction,
            // unreachable by the diffuse bounce ray, so this does not
            // double-count with the sky term on miss.
            float3 lightDir = normalize(-sceneUBO.dirLight.direction.xyz);
            float nDotL = dot(shadingNormal, lightDir);
            if (nDotL > 0.0)
            {
                // Deliberately not forced opaque, same reason as the bounce
                // query above. RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH still
                // ends the search at the first *committed* hit - a rejected
                // (masked-out) candidate keeps traversal going instead of
                // ending the search on a hit that should not count.
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadowQuery;
                RayDesc shadowRay;
                shadowRay.Origin = rayOrigin;
                shadowRay.Direction = lightDir;
                shadowRay.TMin = 0.001;
                shadowRay.TMax = 10000.0;
                shadowQuery.TraceRayInline(
                    TLAS,
                    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                    0xFFu,
                    shadowRay);
                while (shadowQuery.Proceed())
                {
                    if (shadowQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
                    {
                        uint candidateObjIndex = shadowQuery.CandidateInstanceIndex() + shadowQuery.CandidateGeometryIndex();
                        ObjectDescription candidateObj = fetch_object_description(candidateObjIndex);
                        if (!ray_hit_masked_out(candidateObj, shadowQuery.CandidatePrimitiveIndex(), shadowQuery.CandidateTriangleBarycentrics()))
                        {
                            shadowQuery.CommitNonOpaqueTriangleHit();
                        }
                    }
                }
                if (shadowQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
                {
                    // The Lambertian BRDF is albedo/pi; the throughput
                    // already carries the albedo, so NEE must carry the
                    // 1/pi. The bounce direction below needs no such factor:
                    // cosine-weighted sampling of a Lambertian cancels both
                    // the pi and the cosine exactly.
                    const float INV_PI = 0.31830988;
                    float3 lightColor = sceneUBO.dirLight.color.rgb;
                    float lightIntensity = sceneUBO.dirLight.color.w;
                    radiance += throughput * nDotL * INV_PI * lightColor * lightIntensity;
                }
            }

            // Cosine-weighted-ish hemisphere bounce direction (Ray Tracing
            // in One Weekend 8.5: a random point on the unit sphere centered
            // at the normal tip).
            float theta = 6.2831853 * stepAndOutputRNGFloat(rngState);
            float u = 2.0 * stepAndOutputRNGFloat(rngState) - 1.0;
            float r = sqrt(max(1.0 - u * u, 0.0));
            float3 newDirection = hitWorldNormal + float3(r * cos(theta), r * sin(theta), u);

            // RTIOW 9.4: if the random unit vector lands (numerically) on
            // -normal, the sum degenerates to a near-zero direction. Fall
            // back to the normal.
            if (dot(newDirection, newDirection) < 1e-8)
            {
                newDirection = hitWorldNormal;
            }
            rayDirection = newDirection;
        }

        summedColor += radiance;
    }

    float3 avgColor = summedColor / float(numSamples);

    // Temporal accumulation: running mean across frames.
    if (pc_ray.frame_index > 0)
    {
        float4 prev = accumulationImage[tid.xy];
        float3 accumColor = (prev.rgb * float(pc_ray.frame_index) + avgColor) / float(pc_ray.frame_index + 1);
        accumulationImage[tid.xy] = float4(accumColor, 1.0);
        image[tid.xy] = float4(accumColor, 1.0);
    }
    else
    {
        accumulationImage[tid.xy] = float4(avgColor, 1.0);
        image[tid.xy] = float4(avgColor, 1.0);
    }
}
