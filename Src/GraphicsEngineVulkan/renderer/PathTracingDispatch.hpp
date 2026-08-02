#pragma once

#include <cstdint>

// Dispatch-grid constants shared between PathTracing.cpp and
// buildIntegritySuite.cpp's BuildIntegrity.PathTracingDispatchMatchesTheShaderWorkgroupSize
// gate, which pins these against the [numthreads(...)] attribute in
// path_tracing.slang. Plain header (not the PathTracing module interface)
// because buildIntegritySuite.cpp includes plain headers, not modules - see
// CloudDispatch.hpp for the same shape.
namespace Kataglyphis {

inline constexpr uint32_t kPathTracingWorkgroupSizeX = 8;
inline constexpr uint32_t kPathTracingWorkgroupSizeY = 8;

}// namespace Kataglyphis
