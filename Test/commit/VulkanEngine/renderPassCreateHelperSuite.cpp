// Direct unit coverage for common/RenderPassHelper.hpp's
// buildRenderPassCreateInfo - the helper that replaced five hand-written
// vk::RenderPassCreateInfo blocks across Rasterizer, PostStage,
// DeferredRasterizer, SkyBox and CascadedShadowMap.
//
// SkyBox's original call site hard-coded attachmentCount = 2 instead of
// deriving it from the span it was handed - AttachmentCountIsDerivedFromTheSpan
// below is the regression test for exactly that bug.

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vulkan/vulkan.hpp>

#include "common/RenderPassHelper.hpp"

using Kataglyphis::buildRenderPassCreateInfo;

namespace {
constexpr std::array<vk::AttachmentDescription, 3> kThreeAttachments{
    vk::AttachmentDescription{}, vk::AttachmentDescription{}, vk::AttachmentDescription{}
};
constexpr std::array<vk::SubpassDescription, 1> kOneSubpass{ vk::SubpassDescription{} };
constexpr std::array<vk::SubpassDependency, 1> kOneDependency{ vk::SubpassDependency{} };
}// namespace

static_assert(buildRenderPassCreateInfo(std::span<const vk::AttachmentDescription>(kThreeAttachments),
                std::span<const vk::SubpassDescription>(kOneSubpass),
                std::span<const vk::SubpassDependency>(kOneDependency))
                .attachmentCount
    == 3U,
  "buildRenderPassCreateInfo must be usable in a constant expression");

namespace {

TEST(RenderPassCreateHelperUnit, AttachmentCountIsDerivedFromTheSpan)
{
    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.attachmentCount, 3U);
}

TEST(RenderPassCreateHelperUnit, SubpassAndDependencyCountsAreDerivedFromTheirSpans)
{
    // The DeferredRasterizer shape: two subpasses, three dependencies.
    const std::array<vk::SubpassDescription, 2> two_subpasses{ vk::SubpassDescription{}, vk::SubpassDescription{} };
    const std::array<vk::SubpassDependency, 3> three_dependencies{
        vk::SubpassDependency{}, vk::SubpassDependency{}, vk::SubpassDependency{}
    };

    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(two_subpasses), std::span<const vk::SubpassDependency>(three_dependencies));

    EXPECT_EQ(info.subpassCount, 2U);
    EXPECT_EQ(info.dependencyCount, 3U);
}

TEST(RenderPassCreateHelperUnit, PointersPointAtTheCallersStorage)
{
    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.pAttachments, kThreeAttachments.data());
    EXPECT_EQ(info.pSubpasses, kOneSubpass.data());
    EXPECT_EQ(info.pDependencies, kOneDependency.data());
}

TEST(RenderPassCreateHelperUnit, SingleAttachmentPassStillReportsOne)
{
    // The CascadedShadowMap shape: one depth attachment, no colour.
    const std::array<vk::AttachmentDescription, 1> one_attachment{ vk::AttachmentDescription{} };

    const vk::RenderPassCreateInfo info =
      buildRenderPassCreateInfo(std::span<const vk::AttachmentDescription>(one_attachment),
        std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.attachmentCount, 1U);
    EXPECT_EQ(info.pAttachments, one_attachment.data());
}

TEST(RenderPassCreateHelperUnit, FlagsAndPNextAreDefaultedSoCallersCanChainTheirOwn)
{
    // CascadedShadowMap assigns pNext on the returned value to chain a
    // vk::RenderPassMultiviewCreateInfo - that is only safe if the helper
    // itself leaves both fields untouched.
    const vk::RenderPassCreateInfo info = buildRenderPassCreateInfo(
      std::span<const vk::AttachmentDescription>(kThreeAttachments),
      std::span<const vk::SubpassDescription>(kOneSubpass), std::span<const vk::SubpassDependency>(kOneDependency));

    EXPECT_EQ(info.flags, vk::RenderPassCreateFlags{});
    EXPECT_EQ(info.pNext, nullptr);
}

}// namespace

namespace {
namespace fs = std::filesystem;

// Tests run with the repo root as working directory (gtest_discover_tests sets
// WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}), but be forgiving if that changes.
fs::path find_repo_root()
{
    fs::path candidate = fs::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (fs::exists(candidate / "Resources" / "ShadersSlang")) { return candidate; }
        if (!candidate.has_parent_path()) { break; }
        candidate = candidate.parent_path();
    }
    return {};
}

std::string read_file(const fs::path &path)
{
    std::ifstream file(path);
    if (!file) { return {}; }
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

}// namespace

// PostStage and SkyBox used to each allocate, clear and synchronize a depth
// attachment that nothing ever sampled or otherwise read - reclaimed by the
// backlog entry this test pins down. A source-level gate rather than a GPU
// test, since the point is that the render passes no longer *declare* a
// depth attachment at all, not that a particular pixel changed.
TEST(RenderPassCreateHelperUnit, PostAndSkyboxPassesDeclareNoDepthAttachment)
{
    const fs::path repo_root = find_repo_root();
    ASSERT_FALSE(repo_root.empty()) << "could not locate the repository root";

    const fs::path post_stage_path = repo_root / "Src" / "GraphicsEngineVulkan" / "renderer" / "PostStage.cpp";
    const fs::path sky_box_path =
      repo_root / "Src" / "GraphicsEngineVulkan" / "scene" / "sky_box" / "SkyBox.cpp";

    const std::string post_stage_contents = read_file(post_stage_path);
    const std::string sky_box_contents = read_file(sky_box_path);
    ASSERT_FALSE(post_stage_contents.empty()) << "missing or empty " << post_stage_path.string();
    ASSERT_FALSE(sky_box_contents.empty()) << "missing or empty " << sky_box_path.string();

    EXPECT_EQ(post_stage_contents.find("eDepthStencilAttachmentWrite"), std::string::npos)
      << "PostStage.cpp still references eDepthStencilAttachmentWrite - see backlog entry "
         "\"Delete the depth attachment that PostStage and SkyBox allocate, clear and synchronize but never test "
         "or write\"";
    EXPECT_EQ(post_stage_contents.find("depthBufferImage"), std::string::npos)
      << "PostStage.cpp still references depthBufferImage - see the same backlog entry";
    EXPECT_EQ(post_stage_contents.find("depth_attachment"), std::string::npos)
      << "PostStage.cpp's render-pass creation still declares a depth_attachment - see the same backlog entry";

    EXPECT_EQ(sky_box_contents.find("eDepthStencilAttachmentWrite"), std::string::npos)
      << "SkyBox.cpp still references eDepthStencilAttachmentWrite - see the same backlog entry";
    EXPECT_EQ(sky_box_contents.find("depthAttachment"), std::string::npos)
      << "SkyBox.cpp's render-pass creation still declares a depthAttachment - see the same backlog entry";

    // The gate above must not pass by deleting depth synchronization
    // everywhere - Rasterizer and DeferredRasterizer each own a real,
    // sampled-or-tested depth buffer and must keep synchronizing it.
    const fs::path rasterizer_path = repo_root / "Src" / "GraphicsEngineVulkan" / "renderer" / "Rasterizer.cpp";
    const fs::path deferred_rasterizer_path =
      repo_root / "Src" / "GraphicsEngineVulkan" / "renderer" / "DeferredRasterizer.cpp";

    const std::string rasterizer_contents = read_file(rasterizer_path);
    const std::string deferred_rasterizer_contents = read_file(deferred_rasterizer_path);
    ASSERT_FALSE(rasterizer_contents.empty()) << "missing or empty " << rasterizer_path.string();
    ASSERT_FALSE(deferred_rasterizer_contents.empty()) << "missing or empty " << deferred_rasterizer_path.string();

    EXPECT_NE(rasterizer_contents.find("eDepthStencilAttachmentOptimal"), std::string::npos)
      << "Rasterizer.cpp no longer declares a depth attachment - this gate must not pass by deleting depth "
         "synchronization everywhere";
    EXPECT_NE(deferred_rasterizer_contents.find("eDepthStencilAttachmentWrite"), std::string::npos)
      << "DeferredRasterizer.cpp no longer synchronizes its depth attachment - this gate must not pass by "
         "deleting depth synchronization everywhere";
}
