// Pins blasTriangleLimits' contract: maxVertex is the highest addressable
// vertex index (count - 1), never the vertex count itself, and an empty mesh
// must not wrap to 0xFFFFFFFF.

#include <gtest/gtest.h>

#include "renderer/accelerationStructures/BlasGeometryLimits.hpp"

static_assert(Kataglyphis::blasTriangleLimits(100, 300).maxVertex == 99,
  "blasTriangleLimits must be usable in a constant expression");

namespace {

TEST(BlasGeometryLimitsUnit, MaxVertexIsTheHighestIndexNotTheCount)
{
    EXPECT_EQ(Kataglyphis::blasTriangleLimits(100, 300).maxVertex, 99U);
}

TEST(BlasGeometryLimitsUnit, AnEmptyMeshDoesNotWrapToUintMax)
{
    EXPECT_EQ(Kataglyphis::blasTriangleLimits(0, 0).maxVertex, 0U);
    EXPECT_NE(Kataglyphis::blasTriangleLimits(0, 0).maxVertex, 0xFFFFFFFFU);
}

TEST(BlasGeometryLimitsUnit, PrimitiveCountTruncatesAPartialTriangle)
{
    EXPECT_EQ(Kataglyphis::blasTriangleLimits(4, 7).primitiveCount, 2U);
}

}// namespace
