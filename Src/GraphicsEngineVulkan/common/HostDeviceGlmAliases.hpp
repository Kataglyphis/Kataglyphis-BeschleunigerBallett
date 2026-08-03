#pragma once

// GLSL/Slang type aliases shared by the host-side mirrors of the shader
// push-constant and UBO structs (PushConstant*.hpp, GlobalUBO.hpp,
// SceneUBO.hpp). They are deliberately global, not scoped to
// Kataglyphis::VulkanRendererInternals: the whole point is that the struct
// bodies which include this header can be typed exactly like their Slang
// counterparts (vec3, mat4, uint, ...) instead of glm::vec3 / unsigned int.
// This header is C++-only - there is no GLSL/Slang half to guard against,
// the pre-Slang dual-compile shim these aliases used to live inside was
// retired with Resources/Shaders/.
#include <glm/glm.hpp>

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = unsigned int;
