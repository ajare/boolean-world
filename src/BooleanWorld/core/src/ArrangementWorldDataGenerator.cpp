#include "core/ArrangementWorldDataGenerator.h"

#include <algorithm>
#include <map>
#include <stdexcept>
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
  // per-edge override - see ADR-0028).
  std::vector<std::vector<uint32_t>> rawEdgeFlagsPerContour;
  std::vector<std::vector<WallNormalMapOverride>> rawNormalMapsPerContour;
  auto const* meshPrimitive = dynamic_cast<MeshPrimitive const*>(&primitive);

  for (auto const& complexPolygon : primitive.getVertices()) {
    for (auto const& polygon : complexPolygon) {
      arr::Contour contour;
      contour.reserve(polygon.size());
      std::vector<uint32_t> rawEdgeFlags;
      std::vector<WallNormalMapOverride> rawNormalMaps;
      if (meshPrimitive != nullptr) {
        rawEdgeFlags.reserve(polygon.size());
        rawNormalMaps.reserve(polygon.size());
      }
      for (auto const& vertex : polygon) {
        contour.push_back(
            {arr::ToFixedPointCoordinate(vertex.p.x),
             arr::ToFixedPointCoordinate(vertex.p.y)});
        if (meshPrimitive != nullptr) {
          rawEdgeFlags.push_back(vertex.edgeFlags);
          rawNormalMaps.push_back(vertex.edgeNormalMap);
        }
      }
      result.contours.push_back(std::move(contour));
      if (meshPrimitive != nullptr) {
        rawEdgeFlagsPerContour.push_back(std::move(rawEdgeFlags));
        rawNormalMapsPerContour.push_back(std::move(rawNormalMaps));
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
  result.edgeVisibleOverrides.resize(result.contours.size());
  result.edgeNormalMapOverrides.resize(result.contours.size());
  for (size_t c = 0; c < result.contours.size(); ++c) {
    auto const& contour = result.contours[c];
    auto n = contour.size();
    if (n < 2) {
      continue;
    }
    result.edgeOverrides[c].resize(n);
    result.edgeVisibleOverrides[c].resize(n);
    result.edgeNormalMapOverrides[c].resize(n);
    for (size_t i = 0; i < n; ++i) {
      auto j = (i + 1) % n;
      auto useCount = edgeUseCounts[MakeEdgeKey(contour[i], contour[j])];
      if (useCount == 1) {
        auto rawFlags = rawEdgeFlagsPerContour[c][i];
        if ((rawFlags & BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG) != 0) {
          result.edgeOverrides[c][i] = std::optional<bool>(
              (rawFlags & BW_MESH_EDGE_COLLIDES_FLAG) != 0);
        }
        result.edgeVisibleOverrides[c][i] = std::optional<bool>(
            (rawFlags & BW_MESH_EDGE_INVISIBLE_FLAG) == 0);
        auto const& normalMap = rawNormalMapsPerContour[c][i];
        if (normalMap.state() != WallNormalMapOverride::State::Unset) {
          result.edgeNormalMapOverrides[c][i] = normalMap;
        }
      }
    }
  }

  return result;
}

std::vector<arr::ArrangementPrimitive> SnapshotPrimitives(
    std::vector<Primitive*> const& primitives,
    std::vector<uint64_t> const& generatedPriorities,
    ChipParametersResolver const& chipParametersResolver) {
  if (!generatedPriorities.empty() &&
      generatedPriorities.size() != primitives.size()) {
    throw std::invalid_argument(
        "generated priority count must match Primitive count");
  }

  std::vector<arr::ArrangementPrimitive> result;
  result.reserve(primitives.size());
  for (size_t index = 0; index < primitives.size(); ++index) {
    auto* primitive = primitives[index];
    auto contours = ConvertPrimitiveToContours(*primitive);
    auto const properties = primitive->getProperties();
    auto const chipParameters = chipParametersResolver
                                    ? chipParametersResolver(properties.wallMaterialId)
                                    : ChipGenerationParameters{};
    result.push_back({std::move(contours.contours),
                      primitive->getOperation(),
                      primitive->getFillRule(),
                      generatedPriorities.empty()
                          ? primitive->getPriority()
                          : generatedPriorities[index],
                      primitive->getId(),
                      properties,
                      std::move(contours.edgeOverrides),
                      std::move(contours.edgeVisibleOverrides),
                      chipParameters,
                      primitive->getPropertyContribution() ==
                          Primitive::PropertyContribution::Contributing,
                      std::move(contours.edgeNormalMapOverrides)});
  }
  return result;
}

ArrangementWorldDataGenerator::ArrangementWorldDataGenerator()
    : mWorldData(arr::BuildArrangement({})) {
}

void ArrangementWorldDataGenerator::setChipParametersResolver(
    ChipParametersResolver resolver) {
  mChipParametersResolver = std::move(resolver);
}

void ArrangementWorldDataGenerator::generate(
    World const* world, LayerSelection const& selection) {
  auto entries = selectAndOrderPrimitiveEntries(*world, selection);
  std::vector<Primitive*> primitives;
  std::vector<uint64_t> priorities;
  primitives.reserve(entries.size());
  priorities.reserve(entries.size());
  for (auto const& entry : entries) {
    primitives.push_back(entry.primitive);
    priorities.push_back(entry.priority);
  }
  generateOrdered(primitives, priorities);
}

void ArrangementWorldDataGenerator::generate(
    std::vector<Primitive*> const& primitives) {
  auto ordered = primitives;
  std::stable_sort(
      ordered.begin(), ordered.end(),
      [](Primitive const* left, Primitive const* right) {
        return left->getPriority() < right->getPriority();
      });
  mWorldData = arr::BuildArrangement(
      SnapshotPrimitives(ordered, {}, mChipParametersResolver));
}

void ArrangementWorldDataGenerator::generateOrdered(
    std::vector<Primitive*> const& primitives,
    std::vector<uint64_t> const& generatedPriorities) {
  mWorldData = arr::BuildArrangement(
      SnapshotPrimitives(primitives, generatedPriorities, mChipParametersResolver));
}

arr::ArrangementResultPtr ArrangementWorldDataGenerator::getWorldData() const {
  return mWorldData;
}
}  // namespace bw::core
