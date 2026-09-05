// These two abseil headers MUST come before fuzztest.h. FuzzTest at main
// friend-declares absl::random_internal::{DistributionCaller, MockHelpers}
// in fuzzing_bit_gen.h without including them, and abseil LTS 20260526 no
// longer provides them transitively through bit_gen_ref.h. The fuzztest_*
// library targets get the same fix as a force-include flag
// (third_party/CMakeLists.txt); OUR targets cannot, because a force-include
// flag flows into the synthesized C++20 module BMI compiles of imported
// engine modules, which have no abseil include path. An ordinary include in
// the source is invisible to module synthesis.
#include "absl/random/internal/distribution_caller.h"// IWYU pragma: keep
#include "absl/random/internal/mock_helpers.h"// IWYU pragma: keep

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

TEST(MyTestSuite, OnePlustTwoIsTwoPlusOne) { EXPECT_EQ(1 + 2, 2 + 1); }

void IntegerAdditionCommutes(int a, int b) { EXPECT_EQ(a + b, b + a); }
FUZZ_TEST(MyTestSuite, IntegerAdditionCommutes);