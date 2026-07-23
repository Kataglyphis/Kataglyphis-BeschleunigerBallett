// The RAII Vulkan/VMA-handle wrappers each own exactly one handle and must be
// MOVE-ONLY: a copy would leave two instances believing they own the same
// handle, so both would release it (a double vmaDestroyAllocator at shutdown).
//
// Allocator specifically regressed here once already: it was copyable with a
// defaulted destructor that did NOT release, so the handle was silently leaked
// and a stray copy could have double-freed. This suite pins the ownership
// contract so any future regression fails at COMPILE time in this file - a
// clear, local error - rather than as a hard-to-trace crash during teardown.
//
// The runtime cases use default-constructed instances, which own no handle and
// need no Vulkan device, so they run in the headless container too. The raw
// VmaAllocator handle is deliberately not inspected: its type lives in the
// module's global fragment and is not exported, and the ownership guarantee we
// care about is structural (move-only + relocates cleanly), which the
// static_asserts and the vector-relocation case cover.

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>
#include <vector>

import kataglyphis.vulkan.allocator;

using Kataglyphis::Allocator;

static_assert(!std::is_copy_constructible_v<Allocator>, "Allocator must not be copy-constructible (double-free risk)");
static_assert(!std::is_copy_assignable_v<Allocator>, "Allocator must not be copy-assignable (double-free risk)");
static_assert(std::is_nothrow_move_constructible_v<Allocator>, "Allocator must be nothrow move-constructible");
static_assert(std::is_nothrow_move_assignable_v<Allocator>, "Allocator must be nothrow move-assignable");

TEST(AllocatorOwnership, DefaultConstructAndMoveAreClean)
{
    Allocator source;                    // owns no handle (no device required)
    Allocator moved(std::move(source));  // move-construct
    Allocator target;
    target = std::move(moved);           // move-assign
    // Reaching here - and unwinding the three destructors without a crash -
    // proves the move-only wrapper constructs, moves and releases cleanly.
    SUCCEED();
}

TEST(AllocatorOwnership, RelocatesCleanlyInAVector)
{
    // Vector growth move-constructs existing elements then destroys the
    // originals; a copyable-but-double-freeing wrapper would fault here. With a
    // correct move-only wrapper the moved-from originals are left empty, so
    // their destructors are no-ops.
    std::vector<Allocator> allocators;
    allocators.emplace_back();
    allocators.emplace_back();
    allocators.emplace_back();
    EXPECT_EQ(allocators.size(), 3U);
}
