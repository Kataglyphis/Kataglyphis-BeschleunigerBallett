#pragma once

#include <glm/glm.hpp>

namespace Kataglyphis {

// The direction the directional light travels (see GUI.cpp's "Light
// Direction" slider). The GUI lets all three components be dragged to 0,
// which would normalize to NaN and poison every shader/matrix that consumes
// it; fall back to CascadedShadowMapMath.cpp's existing rule (straight down)
// instead. Call this once on the host so downstream consumers always see a
// unit vector.
inline auto normalizedLightDirection(glm::vec3 raw) -> glm::vec3
{
    if (glm::length(raw) < 1e-6F) { raw = glm::vec3(0.0F, -1.0F, 0.0F); }
    return glm::normalize(raw);
}

}// namespace Kataglyphis
