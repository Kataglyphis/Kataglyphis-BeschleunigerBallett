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
#include <string>

// An example function we want to fuzz test
bool IsPalindrome(const std::string& s) {
    if (s.empty()) return true;
    int left = 0;
    int right = s.size() - 1;
    while (left < right) {
        if (s[left] != s[right]) return false;
        left++;
        right--;
    }
    return true;
}

// Property: Reversing a string and appending it to itself should always produce a palindrome
void ReversingAndAppendingCreatesPalindrome(const std::string& input) {
    std::string reversed(input.rbegin(), input.rend());
    std::string candidate = input + reversed;
    EXPECT_TRUE(IsPalindrome(candidate));
}

FUZZ_TEST(PalindromeFuzzTest, ReversingAndAppendingCreatesPalindrome);
