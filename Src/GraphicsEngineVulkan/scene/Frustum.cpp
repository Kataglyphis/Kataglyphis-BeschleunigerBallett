module;
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>

module kataglyphis.vulkan.frustum;

namespace Kataglyphis {

namespace {
/// Normalising is not required for a sign test, but it is required for the
/// distances to mean anything and it keeps the epsilon in `isVisible`
/// scale-independent. A degenerate row (zero-length normal) is left alone
/// rather than producing NaN - a NaN plane would make every comparison false
/// and cull the entire scene.
glm::vec4 normalizePlane(const glm::vec4 &plane)
{
    const float length = glm::length(glm::vec3(plane));
    if (length < 1e-8F) { return plane; }
    return plane / length;
}
}// namespace

FrustumPlanes extractFrustumPlanes(const glm::mat4 &m)
{
    // Gribb-Hartmann: each plane is a sum or difference of the w row and one
    // other row of the view-projection matrix. GLM is column-major, so m[c][r]
    // is column c, row r - the rows we need are m[0..3][k].
    const glm::vec4 row0{ m[0][0], m[1][0], m[2][0], m[3][0] };
    const glm::vec4 row1{ m[0][1], m[1][1], m[2][1], m[3][1] };
    const glm::vec4 row2{ m[0][2], m[1][2], m[2][2], m[3][2] };
    const glm::vec4 row3{ m[0][3], m[1][3], m[2][3], m[3][3] };

    FrustumPlanes planes{};
    planes[0] = normalizePlane(row3 + row0);// left
    planes[1] = normalizePlane(row3 - row0);// right
    planes[2] = normalizePlane(row3 + row1);// bottom
    planes[3] = normalizePlane(row3 - row1);// top
    // Near uses row2 alone, NOT the OpenGL row3 + row2: this engine builds
    // projections with GLM_FORCE_DEPTH_ZERO_TO_ONE, so the near condition is
    // clip.z >= 0 rather than clip.z >= -clip.w.
    //
    // Worked through for near=0.1, far=100: row2 alone puts the plane at view
    // z = -0.1 (exactly the near plane); row3 + row2 puts it at -0.05. The
    // OpenGL form is therefore too PERMISSIVE here, not wrong-sided - it
    // treats the sliver between the camera and the near plane as visible and
    // costs a few extra draws. It does not admit geometry behind the viewer,
    // and the unit tests cannot tell the two apart. Use the accurate one
    // anyway; the point of a culling plane is to mean what it says.
    planes[4] = normalizePlane(row2);// near
    planes[5] = normalizePlane(row3 - row2);// far
    return planes;
}

namespace {
/// Shared body of the visibility tests. `skipPlane` lets the shadow-caster
/// variant drop the near plane without duplicating the p-vertex logic.
bool visibleAgainstPlanes(const FrustumPlanes &planes, const AABB &box, int skipPlane)
{
    if (!box.isValid()) { return true; }

    for (int index = 0; index < static_cast<int>(planes.size()); ++index) {
        if (index == skipPlane) { continue; }
        const glm::vec4 &plane = planes[static_cast<size_t>(index)];
        const glm::vec3 normal{ plane };
        if (glm::dot(normal, normal) < 1e-16F) { continue; }

        const glm::vec3 positive{
            normal.x >= 0.0F ? box.max.x : box.min.x,
            normal.y >= 0.0F ? box.max.y : box.min.y,
            normal.z >= 0.0F ? box.max.z : box.min.z,
        };

        constexpr float kEpsilon = 1e-4F;
        if (glm::dot(normal, positive) + plane.w < -kEpsilon) { return false; }
    }
    return true;
}

/// Index of the near plane in the array extractFrustumPlanes builds.
constexpr int kNearPlaneIndex = 4;
}// namespace

bool isVisibleAsShadowCaster(const FrustumPlanes &planes, const AABB &box)
{
    return visibleAgainstPlanes(planes, box, kNearPlaneIndex);
}

bool isVisible(const FrustumPlanes &planes, const AABB &box)
{
    return visibleAgainstPlanes(planes, box, -1);
}

AABB transformAABB(const glm::mat4 &model, const AABB &box)
{
    if (!box.isValid()) { return box; }

    // Arvo's center/extent form: a third of the eight-corner walk's
    // arithmetic for a provably identical result. Requires `model` to be
    // AFFINE (bottom row [0 0 0 1]) - every caller supplies a model matrix
    // (Model/Mesh), never a projection, which would need the per-corner
    // homogeneous divide the eight-corner walk performed implicitly.
    const glm::vec3 center = 0.5F * (box.min + box.max);
    const glm::vec3 extents = 0.5F * (box.max - box.min);

    const glm::vec3 newCenter{ model * glm::vec4(center, 1.0F) };
    glm::vec3 newExtents{ 0.0F };
    for (int row = 0; row < 3; ++row) {
        newExtents[row] = std::abs(model[0][row]) * extents.x + std::abs(model[1][row]) * extents.y
                         + std::abs(model[2][row]) * extents.z;
    }

    return AABB{ newCenter - newExtents, newCenter + newExtents };
}

}// namespace Kataglyphis
