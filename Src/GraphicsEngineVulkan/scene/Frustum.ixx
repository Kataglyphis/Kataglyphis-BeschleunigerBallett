module;
#include <array>
#include <glm/glm.hpp>

export module kataglyphis.vulkan.frustum;

export namespace Kataglyphis {

/// An axis-aligned bounding box in some space. `min` must be <= `max`
/// componentwise; a box built from no points is left inverted on purpose so
/// `isValid()` can reject it rather than silently behaving like a point at
/// the origin.
struct AABB
{
    glm::vec3 min{ 0.0F };
    glm::vec3 max{ 0.0F };

    [[nodiscard]] bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
};

/// Six clipping planes in world space, each stored as (nx, ny, nz, d) with
/// the convention `dot(n, p) + d >= 0` meaning "inside".
///
/// Order is left, right, bottom, top, near, far - but nothing should depend
/// on it: the intersection test treats them as an unordered set, which is
/// what makes the extraction robust to a different projection convention.
using FrustumPlanes = std::array<glm::vec4, 6>;

/// Extracts world-space frustum planes from a view-projection matrix
/// (Gribb-Hartmann). Works for any projection the matrix encodes, including
/// this engine's reversed-Y and [0,1] depth conventions, because it reads the
/// planes out of the matrix rather than assuming how they were built.
FrustumPlanes extractFrustumPlanes(const glm::mat4 &viewProjection);

/// Conservative visibility test: false only when the box is provably entirely
/// outside the frustum.
///
/// False positives are fine and expected (a box straddling two planes' outside
/// half-spaces can be reported visible); false NEGATIVES are not, because they
/// delete geometry the camera can see. The test therefore uses the p-vertex
/// ("positive vertex") formulation, which never rejects a box that touches the
/// volume.
bool isVisible(const FrustumPlanes &planes, const AABB &boxWorldSpace);

/// Visibility test for SHADOW CASTERS against a light frustum.
///
/// Identical to [`isVisible`] except that the NEAR plane is ignored, and that
/// difference is a correctness requirement rather than an optimisation.
///
/// A cascade's ortho box is fitted to the camera frustum slice it covers, with
/// only a small padding toward the light. Geometry between the light and that
/// box - a tall object, a ceiling - lies outside the near plane but still
/// casts into the box, because its shadow travels along the box's own depth
/// axis. Culling it produces the classic missing-shadow-from-tall-geometry
/// bug. The side and far planes are safe: something outside them in the
/// light's XY casts its shadow outside too.
bool isVisibleAsShadowCaster(const FrustumPlanes &planes, const AABB &boxWorldSpace);

/// Transforms an object-space AABB by a model matrix and returns the AABB of
/// the result.
///
/// The transformed box is NOT the transform of the corners' min/max: under
/// rotation, an axis-aligned box maps to an oriented box whose axis-aligned
/// bound is larger. Uses the Arvo center/extent form, which is exact for
/// AFFINE transforms (bottom row [0 0 0 1] - every caller supplies a model
/// matrix, never a projection) and never under-estimates - important,
/// because an under-estimated bound culls visible geometry.
AABB transformAABB(const glm::mat4 &model, const AABB &boxObjectSpace);

}// namespace Kataglyphis
