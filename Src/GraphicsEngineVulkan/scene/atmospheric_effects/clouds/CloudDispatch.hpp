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

}// namespace Kataglyphis
