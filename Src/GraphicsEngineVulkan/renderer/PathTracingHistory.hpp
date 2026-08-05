#pragma once

#include <glm/glm.hpp>

namespace Kataglyphis::VulkanRendererInternals {

// Everything that, when changed, invalidates the path tracer's temporal
// accumulation history: the camera view (an existing check) plus the
// directional light's direction and colour/radiance, since
// path_tracing.slang's NEE block - the lightDir/lightColor/lightIntensity
// reads under its "Next-event estimation toward the directional light"
// comment - reads sceneUBO.dirLight and a light change makes the running
// mean blend samples lit by two different lights. The projection is here
// too: path_tracing.slang builds every primary ray from
// globalUBO.inv_projection, so a FOV (or near/far/aspect) change changes
// every ray while the running mean keeps counting frames traced through the
// old one. Deliberately narrow - cloud/shadow/PCF fields in SceneUBO are not
// read by path_tracing.slang, so widening this to the whole UBO would reset
// on changes that do not affect the traced image.
struct PathTracingHistoryKey
{
    glm::mat4 view{ 1.0F };
    glm::mat4 projection{ 1.0F };
    glm::vec4 lightDirection{ 0.0F };
    glm::vec4 lightColorAndRadiance{ 0.0F };
    int samplesPerPixel{ 0 };
    int maxBounces{ 0 };

    friend auto operator==(const PathTracingHistoryKey &, const PathTracingHistoryKey &) -> bool = default;
};

}// namespace Kataglyphis::VulkanRendererInternals
