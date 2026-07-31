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
using Kataglyphis::clampCascadeCount;
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

// The split scheme. shadowDistance decouples "how far do we cast shadows"
// from "how far can the camera see" - fitting cascades to a 150-unit far
// plane for a scene that ends at 36 spends most of the shadow map on nothing.
TEST(CascadedShadowMapUnit, ShadowDistanceClampsTheCascadeRange)
{
    constexpr float kShadowDistance = 60.0F;
    const std::vector<CascadeData> cascades = computeCascadeData(
      kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), kShadowDistance, 0.5F);

    ASSERT_EQ(cascades.size(), kCascades);
    // The last cascade must end exactly on the shadow distance: short of it
    // leaves a band that samples nothing and renders unshadowed.
    EXPECT_NEAR(cascades.back().splitDepth, kShadowDistance, 1e-3F);
    for (const CascadeData &cascade : cascades) { EXPECT_LE(cascade.splitDepth, kShadowDistance + 1e-3F); }
}

TEST(CascadedShadowMapUnit, ShadowDistanceBeyondTheFarPlaneIsClampedToIt)
{
    const std::vector<CascadeData> cascades = computeCascadeData(
      kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), kFar * 10.0F, 0.5F);

    ASSERT_EQ(cascades.size(), kCascades);
    EXPECT_NEAR(cascades.back().splitDepth, kFar, 1e-3F) << "cascades must never extend past what the camera sees";
}

TEST(CascadedShadowMapUnit, ZeroShadowDistanceFallsBackToTheFarPlane)
{
    // The documented escape hatch, and what every existing caller relied on
    // before shadowDistance existed.
    const std::vector<CascadeData> cascades =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.5F);

    ASSERT_EQ(cascades.size(), kCascades);
    EXPECT_NEAR(cascades.back().splitDepth, kFar, 1e-3F);
}

TEST(CascadedShadowMapUnit, LambdaZeroReproducesUniformSplits)
{
    const std::vector<CascadeData> cascades =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.0F);

    ASSERT_EQ(cascades.size(), kCascades);
    for (size_t i = 0; i < cascades.size(); ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(kCascades);
        EXPECT_NEAR(cascades[i].splitDepth, kNear + ((kFar - kNear) * p), 1e-2F)
          << "lambda 0 must be the uniform scheme exactly, so it stays a usable baseline";
    }
}

// Raising lambda pulls the splits toward the camera. This is the property
// that makes near geometry crisp - and the same property that STARVES a
// subject framed from a distance, which measurement showed and intuition did
// not: at lambda 0.85 the debug scene's subject falls into the last cascade
// and its texel density gets 3x worse than uniform. The default is 0.5 for
// that reason; this test exists so raising it is a deliberate act.
TEST(CascadedShadowMapUnit, HigherLambdaPullsSplitsTowardTheCamera)
{
    const std::vector<CascadeData> low =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.1F);
    const std::vector<CascadeData> high =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.9F);

    ASSERT_EQ(low.size(), high.size());
    ASSERT_GE(low.size(), 2U);
    // Every split but the last, which is pinned to the shadow far distance.
    for (size_t i = 0; i + 1 < low.size(); ++i) {
        EXPECT_LT(high[i].splitDepth, low[i].splitDepth) << "cascade " << i << " did not tighten with lambda";
    }
    EXPECT_NEAR(low.back().splitDepth, high.back().splitDepth, 1e-3F);
}

TEST(CascadedShadowMapUnit, OutOfRangeLambdaIsClamped)
{
    const std::vector<CascadeData> below =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, -5.0F);
    const std::vector<CascadeData> uniform =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.0F);
    const std::vector<CascadeData> above =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 5.0F);
    const std::vector<CascadeData> logarithmic =
      computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 1.0F);

    ASSERT_EQ(below.size(), kCascades);
    for (size_t i = 0; i < kCascades; ++i) {
        EXPECT_NEAR(below[i].splitDepth, uniform[i].splitDepth, 1e-3F);
        EXPECT_NEAR(above[i].splitDepth, logarithmic[i].splitDepth, 1e-3F);
        EXPECT_TRUE(is_finite(below[i].viewProjMatrix));
        EXPECT_TRUE(is_finite(above[i].viewProjMatrix));
    }
}

// The point of the whole exercise, asserted rather than claimed in a comment.
//
// A cascade's light-space box is fitted with glm::ortho, whose first row
// scales world X by 2/(right-left). The view part is a rigid transform, so the
// length of the composed matrix's first row recovers that scale, and the box
// width falls out as 2/|row0|. Divided by the shadow map resolution it is
// world units per texel - what actually decides whether an edge is crisp.
TEST(CascadedShadowMapUnit, ShadowDistanceImprovesTexelDensityOverTheSubject)
{
    constexpr float kShadowMapRes = 2048.0F;
    // Where dinosaurs.obj sits along the view axis for the debug camera.
    constexpr float kSubjectNear = 16.2F;
    constexpr float kSubjectFar = 36.5F;

    const auto worst_density = [](const std::vector<CascadeData> &cascades) {
        float near_d = kNear;
        float worst = 0.0F;
        for (const CascadeData &cascade : cascades) {
            const bool covers_subject = !(cascade.splitDepth < kSubjectNear || near_d > kSubjectFar);
            if (covers_subject) {
                const glm::mat4 &m = cascade.viewProjMatrix;
                const float row0 = glm::length(glm::vec3(m[0][0], m[1][0], m[2][0]));
                worst = std::max(worst, (2.0F / row0) / kShadowMapRes);
            }
            near_d = cascade.splitDepth;
        }
        return worst;
    };

    const float fitted_to_far_plane =
      worst_density(computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.0F));
    // The lambda here must match GUISceneSharedVars::cascade_split_lambda, or
    // this measures a configuration nobody runs. It is 0 for a measured
    // reason - see the table in CascadedShadowMap.cpp.
    const float fitted_to_shadow_distance =
      worst_density(computeCascadeData(kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 60.0F, 0.0F));

    ASSERT_GT(fitted_to_far_plane, 0.0F) << "no cascade covered the subject - the test framing is wrong";
    ASSERT_GT(fitted_to_shadow_distance, 0.0F) << "no cascade covered the subject - shadow distance is too short";
    EXPECT_LT(fitted_to_shadow_distance, fitted_to_far_plane)
      << "clamping the shadow range must make the subject's cascade tighter, not looser";
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

namespace {
// Texel-space coordinate of a world point under one cascade's matrix: NDC
// scaled so one shadow-map texel is exactly 1.0. Whole-texel motion of the
// box shows up here as integer deltas.
glm::vec2 texel_space(const CascadeData &cascade, const glm::vec3 &world, uint32_t resolution)
{
    const glm::vec4 clip = cascade.viewProjMatrix * glm::vec4(world, 1.0F);
    return { (clip.x / clip.w) * (static_cast<float>(resolution) / 2.0F),
        (clip.y / clip.w) * (static_cast<float>(resolution) / 2.0F) };
}

glm::mat4 translated_view(const glm::vec3 &offset)
{
    return glm::lookAt(
      glm::vec3(0.0F, 6.0F, 26.0F) + offset, glm::vec3(0.0F, 1.0F, 0.0F) + offset, glm::vec3(0.0F, 1.0F, 0.0F));
}
}// namespace

TEST(CascadedShadowMapUnit, StabilizedCascadesShiftByWholeTexelsUnderCameraMotion)
{
    // The shimmer bug: refitting the ortho box to exact frustum corners every
    // frame means sub-texel camera motion moves every shadow edge by a
    // sub-texel amount, and edges crawl. Stabilized cascades may only move
    // the box in WHOLE texel increments, so a static edge stays on the same
    // texels. Assert exactly that: for a fixed world point, the texel-space
    // delta between two sub-texel camera positions must be (near-)integer.
    constexpr uint32_t kResolution = 2048;
    const glm::vec3 tiny_offset(0.0137F, 0.0F, 0.0F);
    const glm::vec3 probe(0.0F, 1.0F, 10.0F);

    const auto stab_a = computeCascadeData(
      kCascades, translated_view({}), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.5F, kResolution);
    const auto stab_b = computeCascadeData(
      kCascades, translated_view(tiny_offset), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.5F, kResolution);

    const glm::vec2 delta = texel_space(stab_a[0], probe, kResolution) - texel_space(stab_b[0], probe, kResolution);
    EXPECT_NEAR(delta.x, std::round(delta.x), 5e-2F) << "x moved by a fractional texel: " << delta.x;
    EXPECT_NEAR(delta.y, std::round(delta.y), 5e-2F) << "y moved by a fractional texel: " << delta.y;

    // Control - the LEGACY path (no resolution) must show the crawl this
    // feature removes, or the assertions above prove nothing: the same camera
    // pair must produce a fractional shift on at least one axis.
    const auto legacy_a =
      computeCascadeData(kCascades, translated_view({}), kFov, kAspect, kNear, kFar, default_light());
    const auto legacy_b =
      computeCascadeData(kCascades, translated_view(tiny_offset), kFov, kAspect, kNear, kFar, default_light());
    const glm::vec2 legacy_delta =
      texel_space(legacy_a[0], probe, kResolution) - texel_space(legacy_b[0], probe, kResolution);
    const float frac_x = std::abs(legacy_delta.x - std::round(legacy_delta.x));
    const float frac_y = std::abs(legacy_delta.y - std::round(legacy_delta.y));
    EXPECT_GT(std::max(frac_x, frac_y), 5e-2F)
      << "legacy path unexpectedly texel-aligned; the stabilized assertions are vacuous";
}

TEST(CascadedShadowMapUnit, StabilizedBoxSizeIsInvariantUnderCameraMotion)
{
    // The other half of the shimmer: the tight-fit box RESIZES as the camera
    // turns, so the world-size of a texel breathes. The stabilized box is
    // sized from the slice's bounding radius, which depends only on
    // fov/aspect/splits - so the ortho scales must be bit-for-bit stable
    // across translation AND rotation of the camera.
    constexpr uint32_t kResolution = 2048;
    const glm::mat4 view_a = translated_view({});
    const glm::mat4 view_b =
      glm::lookAt(glm::vec3(3.0F, 6.0F, 20.0F), glm::vec3(1.0F, 0.0F, -4.0F), glm::vec3(0.0F, 1.0F, 0.0F));

    const auto cascades_a =
      computeCascadeData(kCascades, view_a, kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.5F, kResolution);
    const auto cascades_b =
      computeCascadeData(kCascades, view_b, kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.5F, kResolution);

    for (uint32_t i = 0; i < kCascades; ++i) {
        // viewProj = ortho * rotation; row norms of the upper 3x3 recover the
        // ortho scales (1/half_extent).
        const auto row_norm = [](const glm::mat4 &m, int row) {
            return glm::length(glm::vec3(m[0][row], m[1][row], m[2][row]));
        };
        EXPECT_NEAR(row_norm(cascades_a[i].viewProjMatrix, 0), row_norm(cascades_b[i].viewProjMatrix, 0), 1e-5F)
          << "cascade " << i << " x scale breathes with camera motion";
        EXPECT_NEAR(row_norm(cascades_a[i].viewProjMatrix, 1), row_norm(cascades_b[i].viewProjMatrix, 1), 1e-5F)
          << "cascade " << i << " y scale breathes with camera motion";
    }
}

// clampCascadeCount is the fix for VUID-VkSubpassDescription2-viewMask-06706 /
// VUID-VkRenderPassMultiviewCreateInfo-pViewMasks-06697: the CSM multiview
// render pass sets viewMask = (1 << numCascades) - 1, so a cascade count whose
// top bit exceeds the device's maxMultiviewViewCount is a non-conformant
// render pass. Both the startup init and handleShadowResolutionChange route
// through this function so device conformance no longer relies on
// MAX_CASCADES happening to be small enough for whatever GPU is present.
TEST(CascadedShadowMapUnit, ClampCascadeCountRespectsBothLimits)
{
    EXPECT_EQ(clampCascadeCount(3U, 3U, 8U), 3U) << "neither limit is binding";
    EXPECT_EQ(clampCascadeCount(3U, 8U, 3U), 3U) << "neither limit is binding, order swapped";
    EXPECT_EQ(clampCascadeCount(3U, 3U, 2U), 2U) << "device limit below MAX_CASCADES must bind";
    EXPECT_EQ(clampCascadeCount(8U, 3U, 8U), 3U) << "GUI slider value above MAX_CASCADES must clamp to it";
}

TEST(CascadedShadowMapUnit, ClampCascadeCountFloorsToOne)
{
    // A device reporting a limit of 0 is not a case any real device produces
    // (the Vulkan spec floors maxMultiviewViewCount at 6), but the function
    // must never return a count of 0 - that is not a renderable state - so
    // pin the floor explicitly rather than relying on it never being hit.
    EXPECT_EQ(clampCascadeCount(3U, 3U, 0U), 1U);
    EXPECT_EQ(clampCascadeCount(0U, 3U, 8U), 1U) << "a requested count of 0 must still floor to 1";
}

TEST(CascadedShadowMapUnit, StabilizedCascadesStillCoverTheirSlice)
{
    // Stability must not cost coverage: every slice corner still lands inside
    // its cascade's box (the one-texel pad exists precisely because snapping
    // can shift the center by up to a texel).
    constexpr uint32_t kResolution = 2048;
    const std::vector<CascadeData> cascades = computeCascadeData(
      kCascades, default_view(), kFov, kAspect, kNear, kFar, default_light(), 0.0F, 0.5F, kResolution);
    ASSERT_EQ(cascades.size(), kCascades);

    float slice_near = kNear;
    for (size_t i = 0; i < cascades.size(); ++i) {
        const float slice_far = cascades[i].splitDepth;
        for (const glm::vec4 &corner : frustum_slice_corners(default_view(), slice_near, slice_far)) {
            const glm::vec4 clip = cascades[i].viewProjMatrix * glm::vec4(glm::vec3(corner), 1.0F);
            ASSERT_GT(std::abs(clip.w), 1e-6F);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            constexpr float kEdge = 1e-4F;
            EXPECT_GE(ndc.x, -1.0F - kEdge) << "cascade " << i;
            EXPECT_LE(ndc.x, 1.0F + kEdge) << "cascade " << i;
            EXPECT_GE(ndc.y, -1.0F - kEdge) << "cascade " << i;
            EXPECT_LE(ndc.y, 1.0F + kEdge) << "cascade " << i;
            EXPECT_GE(ndc.z, 0.0F - kEdge) << "cascade " << i;
            EXPECT_LE(ndc.z, 1.0F + kEdge) << "cascade " << i;
        }
        slice_near = slice_far;
    }
}
