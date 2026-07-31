module;
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

module kataglyphis.vulkan.cascaded_shadow_map;

// Split into its own implementation unit (of the SAME module as
// CascadedShadowMap.cpp) deliberately: these are the pure-math free functions
// that computeCascadeData()'s doc comment already promises are callable
// "without a CascadedShadowMap" - i.e. without a Vulkan device. Static linking
// is per-TU, so as long as this stays a separate .obj from the class methods
// (which pull in Scene/Device/Texture), a consumer that only calls
// computeCascadeData()/clampCascadeCount() never links Scene - and therefore
// never collides with a TU that separately bundles TINYOBJLOADER_IMPLEMENTATION
// (see Test/perf/perfSuite.cpp). Keeping this in the CascadedShadowMap.cpp TU
// pulled the whole class - and Scene, and ObjLoader.cpp's tinyobj
// implementation - into the perf benchmark link.

namespace Kataglyphis {

namespace {
// Free so computeCascadeData() can be called without a CascadedShadowMap (and
// therefore without a Vulkan device) in tests.
std::array<glm::vec4, 8> frustumCornersWorldSpace(const glm::mat4 &proj, const glm::mat4 &view)
{
    const auto inv = glm::inverse(proj * view);

    std::array<glm::vec4, 8> frustumCorners{};
    std::size_t index = 0;
    for (unsigned int x = 0; x < 2; ++x) {
        for (unsigned int y = 0; y < 2; ++y) {
            for (unsigned int z = 0; z < 2; ++z) {
                // X/Y span the NDC cube [-1, 1], but depth does NOT: the engine
                // is built with GLM_FORCE_DEPTH_ZERO_TO_ONE (Vulkan convention),
                // so NDC z runs 0..1.
                const glm::vec4 pt =
                  inv * glm::vec4((2.0F * x) - 1.0F, (2.0F * y) - 1.0F, static_cast<float>(z), 1.0F);
                frustumCorners[index] = pt / pt.w;
                ++index;
            }
        }
    }

    return frustumCorners;
}
}// namespace

uint32_t clampCascadeCount(uint32_t requested, uint32_t maxCascades, uint32_t deviceViewLimit)
{
    uint32_t const clamped = std::min({ requested, maxCascades, deviceViewLimit });
    return std::max<uint32_t>(1U, clamped);
}

ShadowPushConstants makeShadowPush(const glm::mat4 &modelMatrix, uint32_t cascadeIndex)
{
    // Deliberately trivial, and deliberately a named function: this used to be
    // written inline as `glm::mat4(1.0f)`, so the shadow pass rendered casters
    // at the wrong scale and nothing was ever occluded. A unit test now pins
    // that the caller's matrix is what goes to the GPU.
    return ShadowPushConstants{ modelMatrix, cascadeIndex };
}

std::vector<CascadeData> computeCascadeData(uint32_t numCascades,
  const glm::mat4 &cameraView,
  float cameraFov,
  float aspect,
  float nearPlane,
  float farPlane,
  const glm::vec3 &lightDir,
  float shadowDistance,
  float splitLambda,
  uint32_t shadowMapResolution)
{
    std::vector<CascadeData> cascadeData(numCascades);
    if (numCascades == 0U) { return cascadeData; }

    // Shadows are fitted to shadowDistance, NOT to the camera far plane. The
    // two are unrelated: the debug scene ends at ~36 units of view depth while
    // the camera sees 150, so fitting cascades to the far plane spent two
    // thirds of the shadow map on empty space. Measured box widths for that
    // framing, 2048x2048 map:
    //   far plane 150, uniform : 3.80 cm/texel over the scene
    //   distance 60, lambda 0.5: 3.04 cm/texel, and 1.79 for a near subject
    // 0 or negative means "no clamp" - fall back to the far plane.
    const float shadowFar =
      (shadowDistance > 0.0F) ? std::min(shadowDistance, farPlane) : farPlane;
    const float shadowNear = std::min(nearPlane, shadowFar * 0.5F);
    const float lambda = std::clamp(splitLambda, 0.0F, 1.0F);

    std::vector<float> cascadeSplits(numCascades + 1);
    cascadeSplits[0] = shadowNear;

    for (uint32_t i = 1; i < numCascades + 1; i++) {
        // Practical split scheme (Zhang et al.): blend a logarithmic
        // distribution, which matches how perspective projection compresses
        // depth, with a uniform one, which keeps the near cascades from
        // collapsing onto the first metre.
        //
        // lambda is NOT "higher is better", and it defaults to 0 (pure
        // uniform) for a measured reason. Worst cm/texel over the debug
        // scene's subject, which sits at view depth 16-36, shadow distance 60:
        //   lambda 0.00  ->  3.04    lambda 0.25  ->  4.56
        //   lambda 0.15  ->  4.56    lambda 0.50  ->  4.56
        // That is a cliff, not a curve: at lambda 0 the second split lands at
        // 40.0, just past the subject, so it fits in the tighter cascades. Any
        // lambda above 0 pulls that split back to ~35 and spills the subject
        // into the 60-unit last cascade. Tuning lambda against one camera
        // angle is overfitting; the durable win is shadowFar above.
        //
        // It still earns its keep for a camera close to its subject
        // (a 2-12 unit subject: 1.52 cm/texel at lambda 0, 1.01 at 0.35),
        // which is why the knob exists rather than being deleted.
        const float p = static_cast<float>(i) / static_cast<float>(numCascades);
        const float logSplit = shadowNear * std::pow(shadowFar / shadowNear, p);
        const float uniformSplit = shadowNear + ((shadowFar - shadowNear) * p);
        cascadeSplits[i] = (lambda * logSplit) + ((1.0F - lambda) * uniformSplit);
    }
    // The last split must land exactly on shadowFar; the blend above is only
    // accurate to float rounding, and a short final cascade leaves a band of
    // geometry that samples nothing and renders unshadowed.
    cascadeSplits[numCascades] = shadowFar;

    for (uint32_t i = 0; i < numCascades; i++) {
        glm::mat4 const curr_cascade_proj = glm::perspective(glm::radians(cameraFov), aspect, cascadeSplits[i], cascadeSplits[i + 1]);

        std::array<glm::vec4, 8> frustumCornerWorldSpace = frustumCornersWorldSpace(curr_cascade_proj, cameraView);

        glm::vec3 center = glm::vec3(0, 0, 0);
        for (const auto &v : frustumCornerWorldSpace) { center += glm::vec3(v); }
        center /= frustumCornerWorldSpace.size();

        // Radius of the cascade's frustum, used to place the light camera far
        // enough back that the whole cascade sits IN FRONT of it. The eye used
        // to be center - lightDir (one unit away), which put part of the
        // cascade behind the light's near plane.
        float radius = 0.0F;
        for (const auto &v : frustumCornerWorldSpace) {
            radius = std::max(radius, glm::length(glm::vec3(v) - center));
        }

        glm::vec3 light_direction = lightDir;
        if (glm::length(light_direction) < 1e-6F) { light_direction = glm::vec3(0.0F, -1.0F, 0.0F); }
        light_direction = glm::normalize(light_direction);
        const glm::vec3 up_axis =
          (std::abs(light_direction.y) > 0.99F) ? glm::vec3(0.0F, 0.0F, 1.0F) : glm::vec3(0.0F, 1.0F, 0.0F);

        if (shadowMapResolution > 0) {
            // STABILIZED path. Three ingredients, each necessary:
            //
            // 1. A WORLD-FIXED light basis (pure rotation about the origin).
            //    The legacy lookAt is anchored at the slice center, so camera
            //    translation is absorbed into the view matrix in continuous
            //    amounts and no snap applied afterwards can help.
            // 2. A box sized from `radius` - a function of the slice geometry
            //    only (fov/aspect/splits), so its texel footprint never
            //    changes as the camera moves or turns.
            // 3. The box CENTER snapped to whole texels in that fixed basis,
            //    so the box only ever moves in texel increments and a static
            //    shadow edge always lands on the same texels.
            //
            // The box is padded by one texel because the snap can shift the
            // center by up to a texel in each axis - without the pad, slice
            // corners could fall just outside. Depth still fits the corners;
            // near may come out NEGATIVE here (the basis is anchored at the
            // origin, not behind the scene) - glm::ortho is a plain box and
            // accepts that; the legacy 0.01 clamp assumed an eye placed
            // behind everything.
            glm::mat4 const light_basis = glm::lookAt(-light_direction, glm::vec3(0.0F), up_axis);

            float const texel_world = (2.0F * radius) / static_cast<float>(shadowMapResolution);
            glm::vec3 center_ls = glm::vec3(light_basis * glm::vec4(center, 1.0F));
            center_ls.x = std::floor(center_ls.x / texel_world) * texel_world;
            center_ls.y = std::floor(center_ls.y / texel_world) * texel_world;
            float const half_extent = radius + texel_world;

            float snapMinZ = std::numeric_limits<float>::max();
            float snapMaxZ = std::numeric_limits<float>::lowest();
            for (const auto &m : frustumCornerWorldSpace) {
                glm::vec4 const v_light = light_basis * m;
                snapMinZ = std::min(snapMinZ, v_light.z);
                snapMaxZ = std::max(snapMaxZ, v_light.z);
            }
            constexpr float snapZPadding = 10.0F;
            float const snap_near = -snapMaxZ - snapZPadding;
            float snap_far = -snapMinZ + snapZPadding;
            if (snap_far <= snap_near) { snap_far = snap_near + 1.0F; }

            glm::mat4 const snap_projection = glm::ortho(center_ls.x - half_extent,
              center_ls.x + half_extent,
              center_ls.y - half_extent,
              center_ls.y + half_extent,
              snap_near,
              snap_far);

            cascadeData[i].viewProjMatrix = snap_projection * light_basis;
            cascadeData[i].splitDepth = cascadeSplits[i + 1];
            continue;
        }

        glm::mat4 const light_view_matrix =
          glm::lookAt(center - (light_direction * (radius * 2.0F + 10.0F)), center, up_axis);

        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (const auto& m : frustumCornerWorldSpace) {
            glm::vec4 const v_light_view = light_view_matrix * m;
            minX = std::min(minX, v_light_view.x);
            maxX = std::max(maxX, v_light_view.x);
            minY = std::min(minY, v_light_view.y);
            maxY = std::max(maxY, v_light_view.y);
            minZ = std::min(minZ, v_light_view.z);
            maxZ = std::max(maxZ, v_light_view.z);
        }

        // Light view space is right-handed and looks down -Z, so corner z
        // values are NEGATIVE. glm::ortho takes positive near/far DISTANCES:
        // near = -maxZ (closest corner), far = -minZ (farthest). Passing the
        // raw negative values mapped nearly every fragment outside [0,1]
        // depth - measured: only ~5% of visible fragments landed inside the
        // shadow map, which is why the sampled shadow term was noise.
        constexpr float zPadding = 10.0F;// keep casters just outside the box
        float near_distance = std::max(0.01F, -maxZ - zPadding);
        float far_distance = (-minZ) + zPadding;
        if (far_distance <= near_distance) { far_distance = near_distance + 1.0F; }

        glm::mat4 const light_projection =
          glm::ortho(minX, maxX, minY, maxY, near_distance, far_distance);

        cascadeData[i].viewProjMatrix = light_projection * light_view_matrix;
        // The split depth is the far plane of this cascade frustum, but measured in view space depth
        // A simple way is to pass the positive distance
        cascadeData[i].splitDepth = cascadeSplits[i + 1];
    }

    return cascadeData;
}

}// namespace Kataglyphis
