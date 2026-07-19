// CPU-only unit tests for the cascaded shadow map maths.
//
// These exist because the CSM code had NO test coverage at all, and a bug that
// disabled shadows completely - the shadow pass transformed casters by a
// hard-coded identity matrix while the forward pass used the scene's (a scale
// of 60) - lived undetected until it was found by hand. Nothing here needs a
// GPU: computeCascadeData() and makeShadowPush() are free functions precisely
// so they can be exercised without a Vulkan device.

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

import kataglyphis.vulkan.cascaded_shadow_map;

namespace {

using Kataglyphis::CascadeData;
using Kataglyphis::computeCascadeData;
using Kataglyphis::makeShadowPush;

constexpr uint32_t kCascades = 3;
constexpr float kFov = 45.0F;
constexpr float kAspect = 16.0F / 9.0F;
constexpr float kNear = 0.1F;
constexpr float kFar = 150.0F;

glm::mat4 default_view() { return glm::lookAt(glm::vec3(0.0F, 6.0F, 26.0F), glm::vec3(0.0F, 1.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F)); }

glm::vec3 default_light() { return glm::vec3(-0.55F, -1.0F, -0.35F); }

std::vector<CascadeData> default_cascades()
{
    return computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light());
}

bool is_finite(const glm::mat4 &m)
{
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(m[col][row])) { return false; }
        }
    }
    return true;
}

// Corners of the camera frustum slice between two view-space depths.
std::vector<glm::vec4> frustum_slice_corners(const glm::mat4 &view, float near_d, float far_d)
{
    const glm::mat4 proj = glm::perspective(glm::radians(kFov), kAspect, near_d, far_d);
    const glm::mat4 inv = glm::inverse(proj * view);

    std::vector<glm::vec4> corners;
    for (unsigned x = 0; x < 2; ++x) {
        for (unsigned y = 0; y < 2; ++y) {
            // Vulkan NDC depth is 0..1 (GLM_FORCE_DEPTH_ZERO_TO_ONE).
            for (unsigned z = 0; z < 2; ++z) {
                const glm::vec4 pt =
                  inv * glm::vec4((2.0F * x) - 1.0F, (2.0F * y) - 1.0F, static_cast<float>(z), 1.0F);
                corners.push_back(pt / pt.w);
            }
        }
    }
    return corners;
}

}// namespace

TEST(CascadedShadowMapUnit, ProducesRequestedNumberOfCascades)
{
    EXPECT_EQ(default_cascades().size(), kCascades);
    EXPECT_TRUE(computeCascadeData(0U, default_view(), kFov, kAspect, kNear, kFar, default_light()).empty());
}

TEST(CascadedShadowMapUnit, SplitDepthsIncreaseAndEndAtFarPlane)
{
    const std::vector<CascadeData> cascades = default_cascades();
    ASSERT_FALSE(cascades.empty());

    float previous = kNear;
    for (size_t i = 0; i < cascades.size(); ++i) {
        EXPECT_GT(cascades[i].splitDepth, previous) << "cascade " << i << " must extend past the previous split";
        EXPECT_LE(cascades[i].splitDepth, kFar + 1e-3F);
        previous = cascades[i].splitDepth;
    }

    // The last cascade has to reach the far plane, or geometry between the last
    // split and the far plane is silently unshadowed.
    EXPECT_NEAR(cascades.back().splitDepth, kFar, 1e-3F);
}

TEST(CascadedShadowMapUnit, MatricesAreFiniteAndNonDegenerate)
{
    for (const CascadeData &cascade : default_cascades()) {
        EXPECT_TRUE(is_finite(cascade.viewProjMatrix)) << "NaN/inf in a cascade matrix";
        EXPECT_GT(std::abs(glm::determinant(cascade.viewProjMatrix)), 1e-12F) << "cascade matrix collapsed";
    }
}

// THE substantive one: a cascade's light-space box must actually contain the
// slice of camera frustum it is responsible for. If it does not, fragments
// project outside the shadow map and are treated as lit - which is exactly how
// this system failed before (measured: ~5% of fragments landed inside the map).
TEST(CascadedShadowMapUnit, EachCascadeCoversItsOwnFrustumSlice)
{
    const std::vector<CascadeData> cascades = default_cascades();
    ASSERT_EQ(cascades.size(), kCascades);

    float slice_near = kNear;
    for (size_t i = 0; i < cascades.size(); ++i) {
        const float slice_far = cascades[i].splitDepth;
        const std::vector<glm::vec4> corners = frustum_slice_corners(default_view(), slice_near, slice_far);

        for (const glm::vec4 &corner : corners) {
            const glm::vec4 clip = cascades[i].viewProjMatrix * glm::vec4(glm::vec3(corner), 1.0F);
            ASSERT_GT(std::abs(clip.w), 1e-6F);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;

            // The ortho box is fitted to exactly these corners, so they land ON
            // the boundary by construction and float error puts them a hair
            // outside (measured 1.00000024). The invariant is containment, not
            // strict interiority - allow an epsilon, but keep it tight enough
            // that a genuinely mis-sized cascade still fails.
            constexpr float kEdge = 1e-4F;
            EXPECT_GE(ndc.x, -1.0F - kEdge) << "cascade " << i << " x out of range";
            EXPECT_LE(ndc.x, 1.0F + kEdge) << "cascade " << i << " x out of range";
            EXPECT_GE(ndc.y, -1.0F - kEdge) << "cascade " << i << " y out of range";
            EXPECT_LE(ndc.y, 1.0F + kEdge) << "cascade " << i << " y out of range";
            // Vulkan depth range, not OpenGL's [-1,1].
            EXPECT_GE(ndc.z, 0.0F - kEdge) << "cascade " << i << " depth below the light near plane";
            EXPECT_LE(ndc.z, 1.0F + kEdge) << "cascade " << i << " depth beyond the light far plane";
        }

        slice_near = slice_far;
    }
}

TEST(CascadedShadowMapUnit, DegenerateLightDirectionDoesNotProduceGarbage)
{
    // A zero light vector must fall back to a sane direction rather than
    // normalising to NaN and poisoning every matrix.
    const std::vector<CascadeData> cascades =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, glm::vec3(0.0F));

    ASSERT_EQ(cascades.size(), kCascades);
    for (const CascadeData &cascade : cascades) { EXPECT_TRUE(is_finite(cascade.viewProjMatrix)); }
}

TEST(CascadedShadowMapUnit, CascadesRespondToLightDirection)
{
    const std::vector<CascadeData> from_above =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, glm::vec3(0.0F, -1.0F, 0.0F));
    const std::vector<CascadeData> from_the_side =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, glm::vec3(-1.0F, -0.2F, 0.0F));

    ASSERT_EQ(from_above.size(), from_the_side.size());
    // Moving the sun must move the light-space matrices; if it does not, the
    // light direction is not reaching the cascade computation at all.
    bool any_difference = false;
    for (size_t i = 0; i < from_above.size(); ++i) {
        if (from_above[i].viewProjMatrix != from_the_side[i].viewProjMatrix) { any_difference = true; }
    }
    EXPECT_TRUE(any_difference) << "cascade matrices ignore the light direction";
}

// Regression guard for the bug that disabled shadows entirely: the shadow pass
// must transform casters by the scene's model matrix. It previously hard-coded
// glm::mat4(1.0f), so with the scene's scale of 60 the caster was rendered at
// 1/60 size, the depth map stayed at its 1.0 clear value, and nothing was ever
// occluded.
TEST(CascadedShadowMapUnit, ShadowPushCarriesTheSceneModelMatrix)
{
    const glm::mat4 scene_model = glm::scale(glm::mat4(1.0F), glm::vec3(60.0F));

    const Kataglyphis::ShadowPushConstants push = makeShadowPush(scene_model, 2U);

    EXPECT_EQ(push.model, scene_model) << "the shadow pass must use the scene's model matrix, not identity";
    EXPECT_NE(push.model, glm::mat4(1.0F)) << "a non-identity scene matrix must not collapse to identity";
    EXPECT_EQ(push.cascadeIndex, 2U);
}
