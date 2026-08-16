#pragma once

#include <vector>
#include <map>

namespace bw {
namespace core {
class VertexTransformerObject;

struct SerializationWorkData {
  // Size of grid to create when deserializing.  <= 0.0f means no grid
  float accelGridSize{-1.0f};

  // Internal snapshots serialize without changing authoring modification state.
  bool markSerializedUnmodified{true};

  // World files omit the editor-only ghost; editor snapshots retain it.
  bool includeGhostPrimitives{false};

  // World files require authored content; editor snapshots may represent an empty world.
  bool allowEmptyWorld{false};

  // Map VertexTransformer ids to their pointer
  std::map<uint32_t, VertexTransformerObject*> vtoIdToVtoMap;

  // Map VertexTransformer ids to their parent id
  std::map<uint32_t, int32_t> vtoIdToParentMap;
};

}  // namespace core
}  // namespace bw