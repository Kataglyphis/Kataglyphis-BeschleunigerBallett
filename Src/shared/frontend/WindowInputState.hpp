#ifndef KATAGLYPHIS_SHARED_FRONTEND_WINDOW_INPUT_STATE_HPP
#define KATAGLYPHIS_SHARED_FRONTEND_WINDOW_INPUT_STATE_HPP

#include <array>

namespace Kataglyphis::Frontend {
inline constexpr std::size_t window_key_count = 1024;

struct WindowInputState
{
    std::array<bool, window_key_count> keys{};
    float last_x{};
    float last_y{};
    float x_change{};
    float y_change{};
    bool mouse_first_moved{ true };
    bool look_mode_active{ false };
};
}// namespace Kataglyphis::Frontend

#endif