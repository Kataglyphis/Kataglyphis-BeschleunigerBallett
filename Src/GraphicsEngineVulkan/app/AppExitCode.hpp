#pragma once

#include <cstdlib>

namespace Kataglyphis {
// The process exit code for a completed App::run(): EXIT_FAILURE if the frame
// loop aborted (device lost, or a fatal submit/sync failure that forced the
// window closed), EXIT_SUCCESS otherwise. Free of Vulkan/GLFW types so it
// links into a test with no device.
constexpr int appExitCode(bool deviceLost, bool fatalFrameError)
{
    return (deviceLost || fatalFrameError) ? EXIT_FAILURE : EXIT_SUCCESS;
}
}// namespace Kataglyphis
