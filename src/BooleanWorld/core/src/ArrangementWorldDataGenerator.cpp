#include "core/ArrangementWorldDataGenerator.h"

#include <map>
#include <utility>

#include "core/Defines.h"
#include "core/MeshPrimitive.h"
#include "core/Primitive.h"
#include "core/World.h"
#include "core/WorldDataGenerator.h"

namespace bw::core {
namespace {
// Order-independent key for an undirected edge between two fixed-point
// vertices, for the local External-edge use-count below.
using FixedPointVertexKey = std::pair<int64_t, int64_t>;
using EdgeKey = std::pair<FixedPointVertexKey, FixedPointVertexKey>;

EdgeKey MakeEdgeKey(arr::FixedPointVertex const& a, arr::FixedPointVertex const& b) {
  FixedPointVertexKey ka{a.x, a.y};
  FixedPointVertexKey kb{b.x, b.y};
  return ka <= kb ? EdgeKey{ka, kb} : EdgeKey{kb, ka};
}
}  // namespace

PrimitiveContours ConvertPrimitiveToContours(
    Primitive const& primitive) {
  PrimitiveContours result;
  // Per-contour raw edgeFlags, parallel to result.contours, collected only
  // when the primitive is a MeshPrimitive (the sole source of a real
  // per-edge override - see ADR-0022).
  std::vector<std::vector<uint32_t>> rawEdgeFlagsPerContour;
  auto const* meshPrimitive = dynamic_cast<MeshPrimitive const*>(&primitive);

  for (auto const& complexPolygon : primitive.getVertices()) {
    for (auto const& polygon : complexPolygon) {
      arr::Contour contour;
      contour.reserve(polygon.size());
      std::vector<uint32_t> rawEdgeFlags;
      if (meshPrimitive != nullptr) {
        rawEdgeFlags.reserve(polygon.size());
      }
      for (auto const& vertex : polygon) {
        contour.push_back(
            {arr::ToFixedPointCoordinate(vertex.p.x),
             arr::ToFixedPointCoordinate(vertex.p.y)});
        if (meshPrimitive != nullptr) {
          rawEdgeFlags.push_back(vertex.edgeFlags);
        }
      }
      result.contours.push_back(std::move(contour));
      if (meshPrimitive != nullptr) {
        rawEdgeFlagsPerContour.push_back(std::move(rawEdgeFlags));
      }
    }
  }

  if (meshPrimitive == nullptr) {
    return result;
  }

  // Recompute "is this edge External" locally and self-consistently, from
  // this same fixed-point contour data - not from the editor-time proxy,
  // whose rest-pose transform can diverge from getVertices()'s live
  // animation state (see #245 design notes). An edge used exactly once
  // across every contour of this primitive is External.
  std::map<EdgeKey, uint32_t> edgeUseCounts;
  for (auto const& contour : result.contours) {
    auto n = contour.size();
    for (size_t i = 0; i < n; ++i) {
      if (n < 2) {
        continue;
      }
      auto j = (i + 1) % n;
      ++edgeUseCounts[MakeEdgeKey(contour[i], contour[j])];
    }
  }

  result.edgeOverrides.resize(result.contours.size());
  for (size_t c = 0; c < result.contours.size(); ++c) {
    auto const& contour = result.contours[c];
    auto n = contour.size();
    if (n < 2) {
      continue;
    }
    result.edgeOverrides[c].resize(n);
    for (size_t i = 0; i < n; ++i) {
      auto j = (i + 1) % n;
      auto useCount = edgeUseCounts[MakeEdgeKey(contour[i], contour[j])];
      if (useCount == 1) {
        result.edgeOverrides[c][i] = std::optional<bool>(
            (rawEdgeFlagsPerContour[c][i] & BW_MESH_EDGE_COLLIDES_FLAG) != 0);
      }
    }
  }

  return result;
}

std::vector<arr::ArrangementPrimitive> SnapshotPrimitives(
    std::vector<Primitive*> const& primitives) {
  std::vector<arr::ArrangementPrimitive> result;
  result.reserve(primitives.size());
  for (auto primitive : primitives) {
    auto contours = ConvertPrimitiveToContours(*primitive);
    result.push_back({std::move(contours.contours),
                      primitive->getOperation(),
                      primitive->getFillRule(),
                      primitive->getPriority(),
                      primitive->getId(),
                      primitive->getProperties(),
                      std::move(contours.edgeOverrides)});
  }
  return result;
}

ArrangementWorldDataGenerator::ArrangementWorldDataGenerator()
    : mWorldData(arr::BuildArrangement({})) {
}

void ArrangementWorldDataGenerator::generate(
    World const* world, LayerSelection const& selection) {
  generate(selectAndOrderPrimitives(*world, selection));
}

void ArrangementWorldDataGenerator::generate(
    std::vector<Primitive*> const& primitives) {
  mWorldData = arr::BuildArrangement(SnapshotPrimitives(primitives));
}

arr::ArrangementResultPtr ArrangementWorldDataGenerator::getWorldData() const {
  return mWorldData;
}
}  // namespace bw::core
