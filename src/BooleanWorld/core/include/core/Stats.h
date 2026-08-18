#pragma once

#include <cstdint>
#include "core/Platform.h"

namespace bw {
namespace core {
struct PrimitiveProcessingStats {
  // Total number of input Primitives
  uint32_t candidateCount{0};

  // Number of directly-visible (in cone) Primitives
  uint32_t visibleCount{0};

  // Number of Primitives whose vertices were updated
  uint32_t updateVertexCount{0};
};

struct ArrangementStats {
  uint32_t vertexCount{0};
  uint32_t edgeCount{0};
  uint32_t faceCount{0};
  uint32_t triangleCount{0};
  uint32_t wallCount{0};
  uint64_t buildPSLGTimeNs{0};
  uint64_t classificationTimeNs{0};
};

struct GenerationRequestStats {
  // Number of pending generation requests replaced by a newer snapshot.
  uint64_t coalescedRequestCount{0};
};

struct Stats {
  PrimitiveProcessingStats prim;
  ArrangementStats arrangement;
  GenerationRequestStats generationRequests;
};

}  // namespace core
}  // namespace bw