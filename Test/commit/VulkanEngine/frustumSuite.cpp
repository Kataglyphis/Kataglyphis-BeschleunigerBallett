// CPU-only tests for frustum culling.
//
// Culling is the one optimisation that can silently DELETE things the user
// should see, and the failure is invisible in aggregate metrics: a frame that
// is missing an object still renders, still has plausible mean luminance, and
// is faster. So these tests weigh the two error directions differently -
// a false positive (drawing something off-screen) costs time, a false
// negative (culling something visible) is a bug. Several tests below assert
// only that visible things survive.
//
// Nothing here needs a GPU: extractFrustumPlanes/isVisible/transformAABB are
// free functions over matrices, exactly so they can be tested this way.

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

import kataglyphis.vulkan.frustum;

namespace {

using Kataglyphis::AABB;
using Kataglyphis::extractFrustumPlanes;
using Kataglyphis::FrustumPlanes;
using Kataglyphis::isVisible;
using Kataglyphis::transformAABB;

constexpr float kFov = 45.0F;
constexpr float kAspect = 16.0F / 9.0F;
constexpr float kNear = 0.1F;
constexpr float kFar = 100.0F;

// Camera at the origin looking down -Z, matching the engine's convention.
glm::mat4 default_view_projection()
{
    const glm::mat4 view =
      glm::lookAt(glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 0.0F, -1.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 projection = glm::perspective(glm::radians(kFov), kAspect, kNear, kFar);
    return projection * view;
}

AABB box_at(const glm::vec3 &centre, float halfExtent)
{
    return AABB{ centre - glm::vec3(halfExtent), centre + glm::vec3(halfExtent) };
}

}// namespace

TEST(FrustumUnit, ExtractsSixNormalizedPlanes)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());

    ASSERT_EQ(planes.size(), 6U);
    for (size_t i = 0; i < planes.size(); ++i) {
        const glm::vec3 normal{ planes[i] };
        EXPECT_NEAR(glm::length(normal), 1.0F, 1e-4F) << "plane " << i << " is not normalized";
        EXPECT_TRUE(std::isfinite(planes[i].w)) << "plane " << i << " has a non-finite distance";
    }
}

TEST(FrustumUnit, BoxInFrontOfTheCameraIsVisible)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());
    EXPECT_TRUE(isVisible(planes, box_at({ 0.0F, 0.0F, -10.0F }, 1.0F)));
}

// The direction that matters. Each of these is somewhere a naive test gets
// wrong, and every one of them is a visible object that must NOT disappear.
TEST(FrustumUnit, NeverCullsGeometryTheCameraCanSee)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());

    // Straddling the near plane: partly behind the camera, partly in front.
    EXPECT_TRUE(isVisible(planes, AABB{ { -1.0F, -1.0F, -0.5F }, { 1.0F, 1.0F, 0.5F } }))
      << "a box straddling the near plane is partly visible";

    // Enormous box containing the whole frustum - no corner is inside any
    // single plane's positive side in the obvious way, but it is visible.
    EXPECT_TRUE(isVisible(planes, box_at({ 0.0F, 0.0F, 0.0F }, 1000.0F)))
      << "a box enclosing the camera must not be culled";

    // Just inside the far plane.
    EXPECT_TRUE(isVisible(planes, box_at({ 0.0F, 0.0F, -(kFar - 1.0F) }, 0.5F)));

    // Clipping the left edge: centre is outside, part of the box is inside.
    EXPECT_TRUE(isVisible(planes, AABB{ { -12.0F, -1.0F, -20.0F }, { -6.0F, 1.0F, -18.0F } }))
      << "a box crossing the left frustum edge is partly visible";

    // Degenerate/never-initialised bounds must be treated as visible: not
    // knowing an object's size is not a licence to delete it.
    AABB inverted{};
    inverted.min = glm::vec3(1.0F);
    inverted.max = glm::vec3(-1.0F);
    ASSERT_FALSE(inverted.isValid());
    EXPECT_TRUE(isVisible(planes, inverted)) << "unknown bounds must render, not vanish";
}

// Distinguishes the [0,1]-depth near plane (row2, view z = -kNear) from the
// OpenGL form (row3 + row2, view z = -kNear/2) that CullsGeometryOutsideEachPlane
// below cannot tell apart. A box entirely inside the sliver between the two
// candidate planes is culled by row2 but survives row3 + row2.
TEST(FrustumUnit, NearPlaneSitsAtTheProjectionNearDistance)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());

    // View z in [-0.09, -0.06]: closer to the camera than the accurate near
    // plane at z = -kNear = -0.1 (so row2 alone culls it), but farther than
    // the OpenGL-style plane at z = -kNear/2 = -0.05 (so row3 + row2 would
    // let it survive).
    const AABB inSliver{ { -0.01F, -0.01F, -0.09F }, { 0.01F, 0.01F, -0.06F } };
    EXPECT_FALSE(isVisible(planes, inSliver))
      << "a box between the accurate and OpenGL-style near planes must be culled; "
         "if this fails, Frustum.cpp:51 regressed to normalizePlane(row3 + row2)";

    // Well past the near plane: must still be visible, so the assertion above
    // cannot pass by culling everything.
    const AABB pastNearPlane{ { -0.01F, -0.01F, -0.2F }, { 0.01F, 0.01F, -0.15F } };
    EXPECT_TRUE(isVisible(planes, pastNearPlane)) << "geometry safely past the near plane must remain visible";
}

TEST(FrustumUnit, CullsGeometryOutsideEachPlane)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());

    // Behind the camera, at several distances.
    //
    // These do NOT distinguish the [0,1]-depth near plane (row2) from the
    // OpenGL one (row3 + row2) - see NearPlaneSitsAtTheProjectionNearDistance
    // above for a test that does. The OpenGL form only shifts the near plane
    // from -0.1 to -0.05, so it draws a sliver extra rather than admitting
    // anything behind the viewer. Do not read these as guarding that choice;
    // see the comment in Frustum.cpp.
    EXPECT_FALSE(isVisible(planes, box_at({ 0.0F, 0.0F, 10.0F }, 1.0F))) << "geometry behind the camera must be culled";
    EXPECT_FALSE(isVisible(planes, box_at({ 0.0F, 0.0F, 0.05F }, 0.01F)))
      << "geometry just behind the camera must be culled";
    EXPECT_FALSE(isVisible(planes, box_at({ 0.0F, 0.0F, 0.5F }, 0.2F)))
      << "geometry behind the near plane must be culled";

    // Far beyond the far plane.
    EXPECT_FALSE(isVisible(planes, box_at({ 0.0F, 0.0F, -(kFar + 50.0F) }, 1.0F)));

    // Well off each side, at a depth where the frustum is still narrow.
    EXPECT_FALSE(isVisible(planes, box_at({ -100.0F, 0.0F, -10.0F }, 1.0F))) << "far left";
    EXPECT_FALSE(isVisible(planes, box_at({ 100.0F, 0.0F, -10.0F }, 1.0F))) << "far right";
    EXPECT_FALSE(isVisible(planes, box_at({ 0.0F, -100.0F, -10.0F }, 1.0F))) << "far below";
    EXPECT_FALSE(isVisible(planes, box_at({ 0.0F, 100.0F, -10.0F }, 1.0F))) << "far above";
}

// A box that fits the camera frustum exactly must survive. This is what
// catches an inverted plane normal: flip one and this fails while the crude
// "far away is culled" tests above still pass.
TEST(FrustumUnit, BoxSpanningTheFrustumSurvivesEveryPlane)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());

    const float halfHeight = std::tan(glm::radians(kFov) * 0.5F) * 50.0F;
    const float halfWidth = halfHeight * kAspect;
    const AABB spanning{ { -halfWidth * 0.5F, -halfHeight * 0.5F, -60.0F },
        { halfWidth * 0.5F, halfHeight * 0.5F, -40.0F } };

    EXPECT_TRUE(isVisible(planes, spanning));
}

TEST(FrustumUnit, TransformAABBCoversTheRotatedBox)
{
    const AABB unit{ glm::vec3(-1.0F), glm::vec3(1.0F) };

    // 45 degrees about Z: the axis-aligned bound of the rotated box grows to
    // sqrt(2) in x and y. Taking min/max of the transformed min/max corners
    // alone would report 1.0 and clip the corners off.
    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0F), glm::radians(45.0F), glm::vec3(0.0F, 0.0F, 1.0F));
    const AABB rotated = transformAABB(rotation, unit);

    EXPECT_NEAR(rotated.max.x, std::sqrt(2.0F), 1e-4F);
    EXPECT_NEAR(rotated.max.y, std::sqrt(2.0F), 1e-4F);
    EXPECT_NEAR(rotated.min.x, -std::sqrt(2.0F), 1e-4F);
    EXPECT_TRUE(rotated.isValid());
}

TEST(FrustumUnit, TransformAABBHandlesTranslationAndScale)
{
    const AABB unit{ glm::vec3(-1.0F), glm::vec3(1.0F) };
    const glm::mat4 model = glm::translate(glm::mat4(1.0F), glm::vec3(10.0F, 0.0F, -5.0F))
                            * glm::scale(glm::mat4(1.0F), glm::vec3(2.0F));

    const AABB moved = transformAABB(model, unit);

    EXPECT_NEAR(moved.min.x, 8.0F, 1e-4F);
    EXPECT_NEAR(moved.max.x, 12.0F, 1e-4F);
    EXPECT_NEAR(moved.min.z, -7.0F, 1e-4F);
    EXPECT_NEAR(moved.max.z, -3.0F, 1e-4F);
}

// The whole point, end to end: an object moved out of view is culled, and the
// same object moved back is not. Culling must follow the model matrix, which
// is what breaks if a caller tests object-space bounds against world-space
// planes.
TEST(FrustumUnit, CullingFollowsTheModelMatrix)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());
    const AABB object{ glm::vec3(-1.0F), glm::vec3(1.0F) };

    const glm::mat4 inView = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.0F, -20.0F));
    const glm::mat4 wayOffLeft = glm::translate(glm::mat4(1.0F), glm::vec3(-500.0F, 0.0F, -20.0F));

    EXPECT_TRUE(isVisible(planes, transformAABB(inView, object)));
    EXPECT_FALSE(isVisible(planes, transformAABB(wayOffLeft, object)));
}

// The shadow-caster variant, and why it is not just isVisible with a flag.
//
// A cascade's ortho box is fitted to the camera frustum slice it covers, with
// only a small padding toward the light. Geometry BETWEEN the light and that
// box still casts into it - its shadow travels along the box's own depth axis
// - but it sits outside the near plane. Culling it is the classic
// missing-shadow-from-tall-geometry bug, and it looks like the shadow simply
// not existing rather than like a culling error.
TEST(FrustumUnit, ShadowCasterTestIgnoresOnlyTheNearPlane)
{
    // An ORTHOGRAPHIC light matrix, because that is the only thing this
    // function is ever handed - a cascade's light-space view-projection.
    //
    // The distinction matters: under a PERSPECTIVE frustum the side planes
    // also reject anything behind the apex, so dropping the near plane alone
    // would not admit a caster between light and box. Under an ortho box the
    // side planes run parallel to the light direction, which is exactly why
    // dropping the near plane is both sufficient and safe. Testing this
    // against a perspective matrix would assert a property the function does
    // not have and is not asked for.
    const glm::mat4 lightView =
      glm::lookAt(glm::vec3(0.0F, 20.0F, 0.0F), glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 0.0F, -1.0F));
    const glm::mat4 lightProjection = glm::ortho(-10.0F, 10.0F, -10.0F, 10.0F, 1.0F, 40.0F);
    const FrustumPlanes planes = extractFrustumPlanes(lightProjection * lightView);

    // Inside the box: both tests agree.
    const AABB inside = box_at({ 0.0F, 0.0F, 0.0F }, 1.0F);
    EXPECT_TRUE(isVisible(planes, inside));
    EXPECT_TRUE(isVisibleAsShadowCaster(planes, inside));

    // Between the light and the box - above the near plane, still casting
    // straight down into it. This is the whole point of the variant.
    const AABB betweenLightAndBox = box_at({ 0.0F, 35.0F, 0.0F }, 1.0F);
    EXPECT_FALSE(isVisible(planes, betweenLightAndBox)) << "the camera test must still reject it";
    EXPECT_TRUE(isVisibleAsShadowCaster(planes, betweenLightAndBox))
      << "a caster between the light and the cascade still casts into it";

    // Side and far planes stay in force: something outside them in the
    // light's XY casts its shadow outside the box too, and something past the
    // far plane is behind everything the cascade covers.
    EXPECT_FALSE(isVisibleAsShadowCaster(planes, box_at({ -100.0F, 0.0F, 0.0F }, 1.0F))) << "far in light X";
    EXPECT_FALSE(isVisibleAsShadowCaster(planes, box_at({ 100.0F, 0.0F, 0.0F }, 1.0F))) << "far in light X";
    EXPECT_FALSE(isVisibleAsShadowCaster(planes, box_at({ 0.0F, 0.0F, 100.0F }, 1.0F))) << "far in light Y";
    EXPECT_FALSE(isVisibleAsShadowCaster(planes, box_at({ 0.0F, -50.0F, 0.0F }, 1.0F))) << "beyond the far plane";
}

TEST(FrustumUnit, ShadowCasterTestKeepsTheOtherGuarantees)
{
    const FrustumPlanes planes = extractFrustumPlanes(default_view_projection());


    // Unknown bounds must render rather than vanish, same as isVisible.
    AABB unknown{};
    unknown.min = glm::vec3(1.0F);
    unknown.max = glm::vec3(-1.0F);
    ASSERT_FALSE(unknown.isValid());
    EXPECT_TRUE(isVisibleAsShadowCaster(planes, unknown));

    // And a box enclosing everything is still a caster.
    EXPECT_TRUE(isVisibleAsShadowCaster(planes, box_at({ 0.0F, 0.0F, 0.0F }, 1000.0F)));
}
