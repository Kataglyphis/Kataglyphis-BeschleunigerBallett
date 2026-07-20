// Fuzzes SceneConfig's path handling. resolveModelPath takes caller-supplied
// strings and builds filesystem paths from them, walking up to 8 parent
// directories; getAvailableModelPaths scans the model directory. Both are
// reachable from GUI model selection, so malformed or hostile input must not
// crash, hang, or escape the resource tree.

// These two abseil headers MUST come before fuzztest.h. FuzzTest at main
// friend-declares absl::random_internal::{DistributionCaller, MockHelpers}
// in fuzzing_bit_gen.h without including them, and abseil LTS 20260526 no
// longer provides them transitively through bit_gen_ref.h. The fuzztest_*
// library targets get the same fix as a force-include flag
// (ExternalLib/CMakeLists.txt); OUR targets cannot, because a force-include
// flag flows into the synthesized C++20 module BMI compiles of imported
// engine modules, which have no abseil include path. An ordinary include in
// the source is invisible to module synthesis.
#include "absl/random/internal/distribution_caller.h"// IWYU pragma: keep
#include "absl/random/internal/mock_helpers.h"// IWYU pragma: keep

#include <filesystem>
#include <string>

#include "fuzztest/fuzztest.h"

import kataglyphis.vulkan.scene_config;

namespace {

void ResolvingArbitraryPathsNeverCrashes(const std::string &relative_path)
{
    // Overly long inputs only exercise the OS path limit, not our logic.
    if (relative_path.size() > 512) { return; }

    const std::string resolved = sceneConfig::resolveModelPath(relative_path);

    // The contract is "returns something usable as a path", not "the file
    // exists" - a miss legitimately returns the direct candidate.
    (void)resolved.empty();

    std::error_code ignored;
    (void)std::filesystem::exists(resolved, ignored);
}

FUZZ_TEST(SceneConfigFuzz, ResolvingArbitraryPathsNeverCrashes)
  .WithSeeds({ std::string("Models/plane.obj"),
    std::string("Models/does_not_exist.obj"),
    std::string(""),
    std::string("../../../etc/passwd"),
    std::string("Models/\xff\xfe/weird.obj"),
    std::string("C:\\absolute\\path.obj") });

// The model listing must stay consistent no matter how often it is called
// (it is invoked from GUI rendering, i.e. every frame the panel is open).
void ListingModelsIsStable(int repetitions)
{
    const int bounded = (repetitions % 4) + 1;
    const auto first = sceneConfig::getAvailableModelPaths();
    for (int i = 1; i < bounded; ++i) {
        const auto again = sceneConfig::getAvailableModelPaths();
        if (again.size() != first.size()) { std::abort(); }
    }
}

FUZZ_TEST(SceneConfigFuzz, ListingModelsIsStable).WithSeeds({ 1, 3 });

}// namespace
