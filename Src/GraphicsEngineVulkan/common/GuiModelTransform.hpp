#pragma once

#include <span>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Kataglyphis {

// Builds the model matrix the GUI's Position/Rotation drag widgets apply to
// scene model 0. There is deliberately NO scale factor: SceneConfig.cpp's
// getModelMatrix() documents that the old hard-coded 60x scale put the
// camera inside the tiny viking_room mesh (all backfaces, culled -> black
// viewport) and stretched the scene far beyond a cascade's useful
// resolution. Dinosaurs (debug) and crytek-sponza (release) both need no
// scaling, so this helper must not reintroduce one.
//
// Order: start from identity, write position directly into the translation
// column (NOT scaled - matches the original handler), then apply rotation
// Z, then Y, then X, each via glm::rotate on the running matrix.
inline auto makeGuiModelTransform(
  std::span<const float, 3> position, std::span<const float, 3> rotationDegrees) -> glm::mat4
{
    glm::mat4 modelMatrix = glm::mat4(1.0f);

    modelMatrix[3] = glm::vec4(position[0], position[1], position[2], 1.0f);

    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees[2]), glm::vec3(0.0f, 0.0f, 1.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees[1]), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rotationDegrees[0]), glm::vec3(1.0f, 0.0f, 0.0f));

    return modelMatrix;
}

}// namespace Kataglyphis
