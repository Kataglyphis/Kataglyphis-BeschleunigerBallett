// Host-side mirror of the scene UBO. Its layout is pinned against the
// Slang redeclaration by BuildIntegrity.SharedStructOffsetsMatchTheCompiledSpirv.
#pragma once
#include "common/HostDeviceGlmAliases.hpp"
#include "common/host_device_shared_vars.hpp"

#include <glm/gtc/matrix_transform.hpp>
namespace Kataglyphis::VulkanRendererInternals {

struct DirectionalLightData {
    vec4 direction;
    vec4 color; // w = radiance
};

struct SceneUBO
{
    // Directional light
    DirectionalLightData dirLight;

    uint pcfRadius;
    float cascadedShadowIntensity;
    uint numCascades;

    // Slang compiles ConstantBuffer<SceneUBO> as std140 (emitted type
    // SceneUBO_std140), where a vec4 has base alignment 16, so cascadeSplits
    // starts at byte 48 on the GPU. glm's plain (non-aligned) vec4 gives the
    // host struct no such padding, so this member must be explicit here.
    // See SceneUboLayoutUnit and
    // `spirv-dis Resources/ShadersSlang/build/spirv/rasterizer/rasterizer.fs_main.spv`
    // to re-derive these offsets.
    uint _pad_std140_0;

    // Cascaded shadow maps
    vec4 cascadeSplits; // up to 4 cascades
    static_assert(MAX_CASCADES <= 4,
      "cascadeSplits is a single vec4 - a fourth-plus cascade would write past its end");
    mat4 cascadeLightSpaceMatrices[MAX_CASCADES];

    // Camera - packed by SceneUboMarshal.hpp's fillSceneUboCamera. Both .w
    // components are 1.0F filler; no shader reads either past .xyz.
    vec4 view_dir;
    vec4 cam_pos;
    
    // Clouds - packed by SceneUboMarshal.hpp's fillSceneUboClouds, which documents
    // what lands in each component.
    vec4 cloudLightMarch;
    vec4 cloudMeshScale;
    vec4 cloudMeshOffset;
    vec4 cloudParameters;
};
}// namespace Kataglyphis::VulkanRendererInternals
