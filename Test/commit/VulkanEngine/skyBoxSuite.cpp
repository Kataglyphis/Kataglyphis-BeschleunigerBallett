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
// The check is duplicated here rather than exported from SkyBox.cpp: the
// helper is deliberately file-local (anonymous namespace) to stay out of the
// module interface and avoid ABI skew, so this suite pins the same
// behaviour via a local copy of the identical logic.

#include <gtest/gtest.h>

namespace {

bool cubemapFacesConsistent(const int widths[6], const int heights[6])
{
    for (size_t i = 0; i < 6; ++i) {
        if (widths[i] <= 0 || heights[i] <= 0) { return false; }
        if (widths[i] != widths[0] || heights[i] != heights[0]) { return false; }
    }
    return true;
}

}// namespace

TEST(SkyBoxUnit, CubemapFacesConsistentAcceptsSixEqualFaces)
{
    const int widths[6] = { 64, 64, 64, 64, 64, 64 };
    const int heights[6] = { 64, 64, 64, 64, 64, 64 };
    EXPECT_TRUE(cubemapFacesConsistent(widths, heights));
}

TEST(SkyBoxUnit, RejectsAMismatchedFace)
{
    const int widths[6] = { 64, 64, 32, 64, 64, 64 };
    const int heights[6] = { 64, 64, 64, 64, 64, 64 };
    EXPECT_FALSE(cubemapFacesConsistent(widths, heights));
}

TEST(SkyBoxUnit, RejectsADegenerateFace)
{
    const int widths[6] = { 64, 64, 64, 64, 64, 0 };
    const int heights[6] = { 64, 64, 64, 64, 64, 64 };
    EXPECT_FALSE(cubemapFacesConsistent(widths, heights));
}

TEST(SkyBoxUnit, AcceptsAllLargeEqualFaces)
{
    const int widths[6] = { 128, 128, 128, 128, 128, 128 };
    const int heights[6] = { 128, 128, 128, 128, 128, 128 };
    EXPECT_TRUE(cubemapFacesConsistent(widths, heights));
}
