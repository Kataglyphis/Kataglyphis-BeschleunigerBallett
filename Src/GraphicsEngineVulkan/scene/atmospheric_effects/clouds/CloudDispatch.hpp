#pragma once

#include <cstdint>

// Dispatch-grid constants shared between Clouds.cpp and
// buildIntegritySuite.cpp's BuildIntegrity.CloudDispatchGridsMatchTheShaderWorkgroupSizes
// gate, which pins these against the [numthreads(...)] attributes in
// noise.slang and clouds.slang. Plain header (not the Clouds module
// interface) because buildIntegritySuite.cpp includes plain headers, not
// modules - see FormatHelper.hpp for the same shape.
namespace Kataglyphis {

inline constexpr uint32_t kNoiseVolumeExtent = 128;
inline constexpr uint32_t kNoiseWorkgroupSize = 8;
inline constexpr uint32_t kCloudWorkgroupSize = 16;

static_assert(kNoiseVolumeExtent % kNoiseWorkgroupSize == 0,
  "kNoiseVolumeExtent must tile evenly by kNoiseWorkgroupSize, or part of the noise volume goes unwritten");

// Bounds for the two cloud march-step counts, shared by the GUI slider
// (GUI.cpp), the host packer (SceneUboMarshal.hpp's fillSceneUboClouds) and
// clouds.slang's own defensive clamps (BuildIntegrity.CloudMarchStepBoundsMatchTheShaderClamps
// pins the shader literals against these). The minimums are load-bearing,
// not cosmetic: both counts become divisors (clouds.slang's dt/dtL) and a
// zero step count makes every sample position +-inf, the same argument
// kMinCloudMeshExtent already carries for the mesh scale.
inline constexpr int kMinCloudMarchSteps = 4;
inline constexpr int kMaxCloudMarchSteps = 128;
inline constexpr int kMinCloudLightMarchSteps = 1;
inline constexpr int kMaxCloudLightMarchSteps = 128;

}// namespace Kataglyphis
