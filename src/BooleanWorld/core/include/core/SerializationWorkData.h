#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

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

  // Runtime loads require authored content; editor loads and snapshots may represent an empty world.
  bool allowEmptyWorld{false};

  // World deserialization parses Layers without building them standalone;
  // their recipes run once after they are bound to the prospective World.
  // Internal to the nested World -> Layer deserialization protocol.
  bool deferLayerRebuild{false};

  // Preserve an editor-only ghost already owned by the target World and add it
  // to the parsed first PrimitiveField before the initial bound rebuild.
  bool preserveTargetGhostPrimitive{false};

  // Optional status callback for hosts that deserialize on a worker thread.
  // The callback is invoked before each potentially long-running load phase.
  std::function<void(std::string const&)> progress;

  // Map VertexTransformer ids to their pointer
  std::map<uint32_t, VertexTransformerObject*> vtoIdToVtoMap;

  // Map VertexTransformer ids to their parent id
  std::map<uint32_t, int32_t> vtoIdToParentMap;
};

}  // namespace core
}  // namespace bw