#pragma once
#include <cstdint>

namespace Kataglyphis {
// aligned piece of memory appropiately and when necessary return bigger piece
constexpr uint32_t align_up(uint32_t memory, uint32_t alignment)
{
    return (memory + alignment - 1) & ~(alignment - 1);
}
}// namespace Kataglyphis