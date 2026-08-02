// Direct unit coverage for kataglyphis.vulkan.mesh_range::sliceMeshRange - the
// helper both loaders call in uploadParsed to cut one sub-mesh out of the flat
// vertex/index/material arrays. It is otherwise only exercised indirectly
// (through GPU rendering of multi-mesh models), so these tests pin the two
// things that are easy to get wrong when slicing: the per-index re-basing by
// vertexBase, and the half-open range bounds.

#include <gtest/gtest.h>

#include <vector>

#include <glm/glm.hpp>

import kataglyphis.vulkan.mesh_range;
import kataglyphis.vulkan.vertex;

using Kataglyphis::MeshRange;
using Kataglyphis::MeshSlice;
using Kataglyphis::sliceMeshRange;
// Vertex lives in the global namespace (Src/shared/scene/Vertex.hpp), not Kataglyphis.
using ::Vertex;

namespace {
Vertex vertexAtX(float x) { return Vertex(glm::vec3(x, 0.0F, 0.0F), glm::vec3(0.0F), glm::vec3(1.0F), glm::vec2(0.0F)); }
}// namespace

TEST(MeshRangeSlice, RebasesIndicesAndSlicesArraysForASubMesh)
{
    // Six vertices identified by position.x = 0..5.
    std::vector<Vertex> vertices;
    for (int i = 0; i < 6; ++i) { vertices.push_back(vertexAtX(static_cast<float>(i))); }

    // A global index buffer holding three triangles; the last two reference the
    // second sub-mesh's vertices (3,4,5).
    const std::vector<unsigned int> indices = { 0, 1, 2, 3, 4, 5, 3, 5, 4 };
    // One material id per triangle.
    const std::vector<unsigned int> materialIndex = { 0, 1, 1 };

    // Second sub-mesh: vertices [3,6), indices [3,9) (six of them), tris [1,3).
    const MeshRange range{ 3, 3, 3, 6, 1, 2 };

    const MeshSlice slice = sliceMeshRange(range, vertices, indices, materialIndex);

    ASSERT_EQ(slice.vertices.size(), 3U);
    EXPECT_FLOAT_EQ(slice.vertices[0].get_position().x, 3.0F);
    EXPECT_FLOAT_EQ(slice.vertices[1].get_position().x, 4.0F);
    EXPECT_FLOAT_EQ(slice.vertices[2].get_position().x, 5.0F);

    // Each index is re-based by vertexBase (3): global 3,4,5,3,5,4 -> 0,1,2,0,2,1.
    const std::vector<unsigned int> expectedIndices = { 0, 1, 2, 0, 2, 1 };
    EXPECT_EQ(slice.indices, expectedIndices);

    // materialIndex[triStart, triStart + triCount) = [1,3) = {1,1}.
    const std::vector<unsigned int> expectedMaterials = { 1, 1 };
    EXPECT_EQ(slice.materialIndex, expectedMaterials);
}

TEST(MeshRangeSlice, SingleRangeSpanningEverythingReproducesTheInputs)
{
    // The single-mesh case: one range covering everything with vertexBase 0. The
    // re-basing subtracts 0, so the sliced arrays must equal the originals -
    // behaviour-identical to not splitting at all.
    std::vector<Vertex> vertices;
    for (int i = 0; i < 3; ++i) { vertices.push_back(vertexAtX(static_cast<float>(i))); }
    const std::vector<unsigned int> indices = { 0, 1, 2 };
    const std::vector<unsigned int> materialIndex = { 0 };
    const MeshRange range{ 0, 3, 0, 3, 0, 1 };

    const MeshSlice slice = sliceMeshRange(range, vertices, indices, materialIndex);

    ASSERT_EQ(slice.vertices.size(), 3U);
    EXPECT_EQ(slice.indices, indices);
    EXPECT_EQ(slice.materialIndex, materialIndex);
}

TEST(MeshRangeSlice, OutOfRangeRangeYieldsAnEmptySliceInsteadOfReadingPastTheArrays)
{
    // Three vertices, three indices, one material id - a well-formed
    // single-triangle input. Each sub-test below breaks exactly one of the
    // three range checks; sliceMeshRange must refuse to read past the arrays
    // and return an empty MeshSlice instead of crashing/UB.
    std::vector<Vertex> vertices;
    for (int i = 0; i < 3; ++i) { vertices.push_back(vertexAtX(static_cast<float>(i))); }
    const std::vector<unsigned int> indices = { 0, 1, 2 };
    const std::vector<unsigned int> materialIndex = { 0 };

    // vertexBase + vertexCount > vertices.size() (3 + 1 > 3).
    {
        const MeshRange range{ 3, 1, 0, 3, 0, 1 };
        const MeshSlice slice = sliceMeshRange(range, vertices, indices, materialIndex);
        EXPECT_TRUE(slice.vertices.empty());
        EXPECT_TRUE(slice.indices.empty());
        EXPECT_TRUE(slice.materialIndex.empty());
    }

    // indexStart + indexCount > indices.size() (1 + 3 > 3).
    {
        const MeshRange range{ 0, 3, 1, 3, 0, 1 };
        const MeshSlice slice = sliceMeshRange(range, vertices, indices, materialIndex);
        EXPECT_TRUE(slice.vertices.empty());
        EXPECT_TRUE(slice.indices.empty());
        EXPECT_TRUE(slice.materialIndex.empty());
    }

    // triStart + triCount > materialIndex.size() (0 + 2 > 1).
    {
        const MeshRange range{ 0, 3, 0, 3, 0, 2 };
        const MeshSlice slice = sliceMeshRange(range, vertices, indices, materialIndex);
        EXPECT_TRUE(slice.vertices.empty());
        EXPECT_TRUE(slice.indices.empty());
        EXPECT_TRUE(slice.materialIndex.empty());
    }
}
