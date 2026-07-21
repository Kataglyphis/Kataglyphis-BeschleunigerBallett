#pragma once

#include <gtest/gtest.h>

namespace Kataglyphis::TestSupport {

/// Renders frames until the startup model has been installed.
///
/// The model is parsed on a worker thread, so a freshly constructed renderer
/// draws an empty scene for the ~2.8 s the parse takes. Every test that asserts
/// on geometry - pixels, culling counts, object descriptions - must pump the
/// frame loop until the model arrives, because the poll that installs it lives
/// in updateStateDueToUserInput.
///
/// `renderOneFrame` is whatever one iteration of the caller's loop is; suites
/// differ in how much of App::run they reproduce.
///
/// The frame cap is a deadlock guard, not a timing assumption: a parse that
/// never completes must fail the test rather than hang CI forever.
///
/// `Renderer` is a template parameter rather than a concrete
/// `import kataglyphis.vulkan.renderer;` ON PURPOSE. A header that imports a
/// module and is then included ahead of a textual `#include <vulkan/vulkan.hpp>`
/// (as the GPU suites do) makes gcc's -fmodules-ts see VkBuffer_T declared both
/// module-owned and textually and reject it as a "conflicting declaration" - the
/// clang builds tolerate it, the gcc lane does not. Deducing the renderer type
/// from the argument keeps this header import-free; every caller already imports
/// the renderer module for its own use.
template<typename Renderer, typename RenderOneFrame>
void waitForModelLoad(Renderer *renderer, RenderOneFrame renderOneFrame)
{
    constexpr int MAX_LOAD_FRAMES = 20000;

    int frames = 0;
    while (renderer->isModelLoadPending() && frames < MAX_LOAD_FRAMES) {
        renderOneFrame();
        ++frames;
        if (renderer->hasDeviceLost()) { return; }
    }

    EXPECT_FALSE(renderer->isModelLoadPending())
      << "Model was still loading after " << MAX_LOAD_FRAMES << " frames.";
}

}// namespace Kataglyphis::TestSupport
