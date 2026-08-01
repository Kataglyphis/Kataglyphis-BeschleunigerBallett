// Pins align_up's rounding contract and that it is a genuine constexpr
// function (a namespace-scope static_assert only compiles if the compiler
// can evaluate the call at compile time).

#include <gtest/gtest.h>

#include "common/MemoryHelper.hpp"

static_assert(Kataglyphis::align_up(65, 64) == 128, "align_up must be usable in a constant expression");

namespace {

TEST(MemoryHelperUnit, AlreadyAlignedInputIsUnchanged) { EXPECT_EQ(Kataglyphis::align_up(64, 64), 64U); }

TEST(MemoryHelperUnit, PartialValueRoundsUpToNextAlignment)
{
    EXPECT_EQ(Kataglyphis::align_up(65, 64), 128U);
    EXPECT_EQ(Kataglyphis::align_up(1, 64), 64U);
}

TEST(MemoryHelperUnit, ZeroStaysZero) { EXPECT_EQ(Kataglyphis::align_up(0, 64), 0U); }

TEST(MemoryHelperUnit, AlignmentOfOneIsIdentity) { EXPECT_EQ(Kataglyphis::align_up(65, 1), 65U); }

}// namespace
