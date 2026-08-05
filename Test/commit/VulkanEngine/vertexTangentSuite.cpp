// vertex::computeTangents, exercised directly with hand-built triangles - no
// Vulkan device and no glTF/OBJ document needed. gltfParseSuite.cpp covers the
// loader-integration half (authored TANGENT pass-through, generated tangents
// on a real asset); this file pins the maths itself: UV-gradient direction,
// unit length, orthogonality to the normal, and mirrored-UV handedness.

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

import kataglyphis.vulkan.vertex;

namespace {

// A unit quad in the XY plane (two triangles sharing the diagonal), UVs
// running along +U/+V in step with position - the simplest chart with no
// mirroring. `flipU` negates the U axis of one corner's UV to build the
// mirrored-chart fixture the handedness test needs.
std::vector<Vertex> unitQuadVertices(bool flipU = false)
{
    const glm::vec3 normal(0.0F, 0.0F, 1.0F);
    const float u1 = flipU ? -1.0F : 1.0F;
    std::vector<Vertex> vertices;
    vertices.emplace_back(glm::vec3(0.0F, 0.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(0.0F, 0.0F));
    vertices.emplace_back(glm::vec3(1.0F, 0.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(u1, 0.0F));
    vertices.emplace_back(glm::vec3(1.0F, 1.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(u1, 1.0F));
    vertices.emplace_back(glm::vec3(0.0F, 1.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(0.0F, 1.0F));
    return vertices;
}

const std::vector<unsigned int> kQuadIndices = { 0U, 1U, 2U, 0U, 2U, 3U };

}// namespace

TEST(VertexUnit, UnitQuadTangentFollowsUvGradient)
{
    // UVs run along +U in lockstep with +X, so the generated tangent must
    // point along +X (the quad's U direction), be unit length, sit
    // perpendicular to the normal, and carry a right-handed (+1) sign - the
    // quad's chart is not mirrored.
    std::vector<Vertex> vertices = unitQuadVertices();
    vertex::computeTangents(vertices, kQuadIndices);

    for (const Vertex &v : vertices) {
        EXPECT_NEAR(v.tangent.x, 1.0F, 1e-4F) << "tangent should follow +U == +X";
        EXPECT_NEAR(v.tangent.y, 0.0F, 1e-4F);
        EXPECT_NEAR(v.tangent.z, 0.0F, 1e-4F);
        EXPECT_NEAR(glm::length(glm::vec3(v.tangent)), 1.0F, 1e-4F);
        EXPECT_NEAR(glm::dot(glm::vec3(v.tangent), v.normal), 0.0F, 1e-4F);
        EXPECT_EQ(v.tangent.w, 1.0F) << "an unmirrored chart must be right-handed";
    }
}

TEST(VertexUnit, TangentIsUnitAndOrthogonalOnNonAxisAlignedFixtures)
{
    // Suzanne/cube-shaped fixtures are not part of this commit test tree, so
    // the general "unit length + orthogonal to normal" invariant is pinned on
    // synthetic corners whose normal is NOT axis-aligned, closing the gap an
    // axis-aligned-only quad would leave.
    const glm::vec3 normal = glm::normalize(glm::vec3(1.0F, 1.0F, 1.0F));
    std::vector<Vertex> vertices;
    vertices.emplace_back(glm::vec3(0.0F, 0.0F, 1.0F), normal, glm::vec4(1.0F), glm::vec2(0.0F, 0.0F));
    vertices.emplace_back(glm::vec3(1.0F, 0.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(1.0F, 0.0F));
    vertices.emplace_back(glm::vec3(0.0F, 1.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(0.3F, 1.0F));
    const std::vector<unsigned int> indices = { 0U, 1U, 2U };

    vertex::computeTangents(vertices, indices);

    for (const Vertex &v : vertices) {
        EXPECT_NEAR(glm::length(glm::vec3(v.tangent)), 1.0F, 1e-4F) << "|T| must be 1";
        EXPECT_NEAR(glm::dot(glm::vec3(v.tangent), v.normal), 0.0F, 1e-4F) << "dot(T, N) must be ~0";
        EXPECT_TRUE(v.tangent.w == 1.0F || v.tangent.w == -1.0F);
    }
}

TEST(VertexUnit, MirroredUvQuadProducesNegativeHandedness)
{
    // Same quad, but its UV chart is mirrored across U - the geometric
    // bitangent no longer agrees with cross(N, T), so w must flip to -1.
    std::vector<Vertex> mirrored = unitQuadVertices(/*flipU=*/true);
    vertex::computeTangents(mirrored, kQuadIndices);

    for (const Vertex &v : mirrored) { EXPECT_EQ(v.tangent.w, -1.0F) << "a mirrored UV chart must be left-handed"; }
}

TEST(VertexUnit, DegenerateUvFallsBackToAnAxisOrthogonalToTheNormalNotNan)
{
    // Every corner shares the exact same UV: the UV-gradient determinant is
    // zero, so Gram-Schmidt has nothing to project. The fallback must still
    // produce a finite, unit-length, normal-orthogonal tangent - never NaN.
    const glm::vec3 normal(0.0F, 0.0F, 1.0F);
    std::vector<Vertex> vertices;
    vertices.emplace_back(glm::vec3(0.0F, 0.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(0.5F, 0.5F));
    vertices.emplace_back(glm::vec3(1.0F, 0.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(0.5F, 0.5F));
    vertices.emplace_back(glm::vec3(0.0F, 1.0F, 0.0F), normal, glm::vec4(1.0F), glm::vec2(0.5F, 0.5F));
    const std::vector<unsigned int> indices = { 0U, 1U, 2U };

    vertex::computeTangents(vertices, indices);

    for (const Vertex &v : vertices) {
        EXPECT_FALSE(std::isnan(v.tangent.x) || std::isnan(v.tangent.y) || std::isnan(v.tangent.z));
        EXPECT_NEAR(glm::length(glm::vec3(v.tangent)), 1.0F, 1e-4F);
        EXPECT_NEAR(glm::dot(glm::vec3(v.tangent), v.normal), 0.0F, 1e-4F);
    }
}

TEST(VertexUnit, TangentBasisMapsAFlatSampleToTheGeometricNormal)
{
    // Regression fixture for the apply_normal_map() operand-order bug (see
    // common/normal_map.slang): a flat normal-map texel (nTs == (0, 0, 1),
    // "no perturbation") must map back to the geometric normal exactly. This
    // pins *why* BuildIntegrity.NormalMappingIsAppliedByEveryShadingPath's
    // text-match on "mul(nTs, float3x3(" is the correct operand order, by
    // computing both candidate expressions directly (mirroring slang's
    // row-built float3x3(t, b, n) semantics: mul(v, M) = v.x*t + v.y*b +
    // v.z*n, mul(M, v) = (dot(t, v), dot(b, v), dot(n, v))) rather than
    // trusting a glm::mat3 constructor to reproduce them.
    const glm::vec3 n(0.0F, 1.0F, 0.0F);
    const glm::vec3 t(1.0F, 0.0F, 0.0F);
    const float w = 1.0F;
    const glm::vec3 b = glm::cross(n, t) * w;
    const glm::vec3 nTs(0.0F, 0.0F, 1.0F);

    const glm::vec3 rightMultiplied = nTs.x * t + nTs.y * b + nTs.z * n;
    const glm::vec3 leftMultiplied(glm::dot(t, nTs), glm::dot(b, nTs), glm::dot(n, nTs));

    EXPECT_NEAR(glm::length(rightMultiplied - n), 0.0F, 1e-6F)
      << "mul(nTs, float3x3(t, b, n)) must return the geometric normal for a flat normal-map sample";
    EXPECT_GT(glm::length(leftMultiplied - n), 1e-3F)
      << "mul(float3x3(t, b, n), nTs) is the world-to-tangent transform and must NOT return the geometric normal";
}

TEST(VertexUnit, TangentParticipatesInEqualityAndHash)
{
    // Vertex::operator== and its std::hash specialization must both include
    // tangent - two vertices identical except for tangent must compare
    // unequal and (with high probability) hash differently, the same
    // contract color already carries.
    const glm::vec3 pos(1.0F, 2.0F, 3.0F);
    const glm::vec3 normal(0.0F, 1.0F, 0.0F);
    const glm::vec4 color(1.0F);
    const glm::vec2 uv(0.25F, 0.75F);

    const Vertex a(pos, normal, color, uv, glm::vec4(1.0F, 0.0F, 0.0F, 1.0F));
    const Vertex b(pos, normal, color, uv, glm::vec4(0.0F, 1.0F, 0.0F, 1.0F));

    EXPECT_FALSE(a == b);
    EXPECT_NE(std::hash<Vertex>()(a), std::hash<Vertex>()(b));
}
