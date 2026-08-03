module;
#include <cstdint>
#include <optional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "renderer/pushConstants/PushConstantRasterizer.hpp"

module kataglyphis.vulkan.mesh_draw_recorder;

import kataglyphis.vulkan.scene;
import kataglyphis.vulkan.frustum;
import kataglyphis.vulkan.mesh;

Kataglyphis::VulkanRendererInternals::MeshDrawStats
  Kataglyphis::VulkanRendererInternals::recordSceneMeshDraws(vk::CommandBuffer commandBuffer,
    vk::PipelineLayout pipelineLayout,
    vk::ShaderStageFlags pushConstantStages,
    Kataglyphis::Scene *scene,
    const std::optional<FrustumPlanes> &cameraFrustum,
    PushConstantRasterizer &pushConstant)
{
    MeshDrawStats stats{ 0, 0 };

    // object descriptions are flattened one-per-mesh across all models
    // (Scene::add_model), so objectIndex is the running FLAT mesh index, not the
    // model index. It advances for every mesh - culled ones included - to stay
    // aligned with that buffer. Identical to the old per-model push while each
    // Model holds one mesh.
    uint32_t flat_mesh_index = 0;
    for (uint32_t m = 0; m < scene->getModelCount(); m++) {
        const glm::mat4 model_matrix = scene->getModelMatrix(m);
        pushConstant.model = model_matrix;
        // Precompute the inverse-transpose for the Slang shaders (no inverse() in SPIR-V).
        // Only the rows survive the push constant (see PushConstantRasterizer.hpp).
        const glm::mat4 inv_transpose_model = glm::inverse(glm::transpose(model_matrix));
        for (int row = 0; row < 3; ++row) {
            pushConstant.invModelRows[row] =
              glm::vec4(inv_transpose_model[0][row], inv_transpose_model[1][row], inv_transpose_model[2][row], 0.0F);
        }

        for (unsigned int k = 0; k < scene->getMeshCount(m); k++) {
            const uint32_t object_index = flat_mesh_index++;
            ++stats.considered;

            // Unreachable with a null mesh: m/k come from getModelCount()/
            // getMeshCount(m), so findMesh() always resolves here. Kept as a
            // fallback rather than an assert so behaviour matches the former
            // per-accessor calls byte-for-byte.
            Mesh *mesh = scene->findMesh(m, k);
            static const AABB unknownBounds{ glm::vec3(1.0F), glm::vec3(-1.0F) };
            const AABB &meshBounds = mesh != nullptr ? mesh->getBounds() : unknownBounds;

            // Skip meshes provably outside the view. isVisible() is
            // conservative and treats unknown bounds as visible, so this can
            // only ever drop geometry the camera cannot see.
            if (cameraFrustum.has_value()
                && !isVisible(*cameraFrustum, transformAABB(pushConstant.model, meshBounds))) {
                continue;
            }

            pushConstant.objectIndex = object_index;
            commandBuffer.pushConstants(
              pipelineLayout, pushConstantStages, 0, sizeof(PushConstantRasterizer), &pushConstant);

            // glTF material.doubleSided: render both faces for this mesh, else
            // back-face cull. The pipeline declares eCullMode dynamic, so this
            // must be set for every draw (default eBack for OBJ / single-sided).
            commandBuffer.setCullMode(mesh != nullptr && mesh->isDoubleSided() ? vk::CullModeFlagBits::eNone
                                                                                : vk::CullModeFlagBits::eBack);

            const vk::Buffer vertex_buffer = mesh != nullptr ? mesh->getVertexBuffer() : vk::Buffer{};
            const vk::DeviceSize offset = 0;
            commandBuffer.bindVertexBuffers(0, 1, &vertex_buffer, &offset);

            commandBuffer.bindIndexBuffer(
              mesh != nullptr ? mesh->getIndexBuffer() : vk::Buffer{}, 0, vk::IndexType::eUint32);

            commandBuffer.drawIndexed(mesh != nullptr ? mesh->getIndexCount() : 0, 1, 0, 0, 0);
            ++stats.drawn;
        }
    }

    return stats;
}
