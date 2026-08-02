// CPU-only tests for the SkyBox cubemap face-dimension guard.
//
// SkyBox::loadCubeMap has a real memory-safety check: every cubemap face
// must match the first face's dimensions, because the upload copies
// layerSize bytes out of every face. A face larger than the first would read
// off the end of an earlier allocation; a smaller one would write its layers
// at overlapping offsets. That guard lives inside a GPU/file-system path (it
// loads six PNGs), so it was unreachable by any CPU test. This suite drives
// the pure dimension-check function directly.
//
// The guard now has one definition, in the plain header
// scene/sky_box/CubemapFaces.hpp, shared by the SkyBox module TU and this
// suite - there is no local copy to drift out of sync with the shipped code.

#include <gtest/gtest.h>

#include "scene/sky_box/CubemapFaces.hpp"

namespace {
constexpr int kConstexprEqualFaces[6] = { 64, 64, 64, 64, 64, 64 };
}// namespace

static_assert(Kataglyphis::cubemapFacesConsistent(kConstexprEqualFaces, kConstexprEqualFaces),
  "cubemapFacesConsistent must be usable in a constant expression");

TEST(SkyBoxUnit, CubemapFacesConsistentAcceptsSixEqualFaces)
{
    const int widths[6] = { 64, 64, 64, 64, 64, 64 };
    const int heights[6] = { 64, 64, 64, 64, 64, 64 };
    EXPECT_TRUE(Kataglyphis::cubemapFacesConsistent(widths, heights));
}

TEST(SkyBoxUnit, RejectsAMismatchedFace)
{
    const int widths[6] = { 64, 64, 32, 64, 64, 64 };
    const int heights[6] = { 64, 64, 64, 64, 64, 64 };
    EXPECT_FALSE(Kataglyphis::cubemapFacesConsistent(widths, heights));
}

TEST(SkyBoxUnit, RejectsADegenerateFace)
{
    const int widths[6] = { 64, 64, 64, 64, 64, 0 };
    const int heights[6] = { 64, 64, 64, 64, 64, 64 };
    EXPECT_FALSE(Kataglyphis::cubemapFacesConsistent(widths, heights));
}

TEST(SkyBoxUnit, RejectsADegenerateFirstFace)
{
    const int widths[6] = { 0, 64, 64, 64, 64, 64 };
    const int heights[6] = { 64, 64, 64, 64, 64, 64 };
    EXPECT_FALSE(Kataglyphis::cubemapFacesConsistent(widths, heights));
}

TEST(SkyBoxUnit, AcceptsAllLargeEqualFaces)
{
    const int widths[6] = { 128, 128, 128, 128, 128, 128 };
    const int heights[6] = { 128, 128, 128, 128, 128, 128 };
    EXPECT_TRUE(Kataglyphis::cubemapFacesConsistent(widths, heights));
}
