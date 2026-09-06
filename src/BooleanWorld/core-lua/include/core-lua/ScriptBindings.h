#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <sol/sol.hpp>

namespace bw {
namespace core {

class Primitive;
class MeshPrimitive;
class MeshPrimitiveEditingProxy;
class Prefab;
class DefinePrefabs;
class PrimitiveField;
class LayerBuildContext;
class RunScript;

// A mutable handle to an AudioEmitter owned by a script-created Primitive.
// It resolves by deterministic GUID rather than retaining a vector element
// pointer, so adding another emitter cannot invalidate a handle Lua holds.
class ScriptAudioEmitter {
private:
  Primitive* mPrimitive;
  std::string mGuid;

public:
  ScriptAudioEmitter(Primitive* primitive, std::string guid);

  [[nodiscard]] std::tuple<float, float> getOffset() const;
  void setOffset(float x, float y) const;
  [[nodiscard]] float getHeightOffset() const;
  void setHeightOffset(float height) const;
  [[nodiscard]] std::string getSoundId() const;
  void setSoundId(std::string const& soundId) const;

  [[nodiscard]] Primitive* getPrimitive() const;
  [[nodiscard]] std::string const& getGuid() const;
};

// A read-only, non-owning view of a Primitive, handed to scripts for prior
// build Primitives (docs spec #365). sol2 does not track const-ness on a
// bound pointer type - a script holding a `Primitive*` could call any bound
// mutator regardless of the C++ constness of what produced it - so priors are
// wrapped in this type instead, which is bound with accessors only. There is
// no mutating method to call, in Lua or in C++.
struct PrimitiveView {
  Primitive const* primitive;
};

// Wraps borrowed Primitive pointers as read-only PrimitiveView handles.
[[nodiscard]] std::vector<PrimitiveView> toPrimitiveViews(std::vector<Primitive*> const& primitives);

// A read-only, non-owning view of a Prefab (docs spec #366). A script never
// gets a mutable handle to a Prefab or its Primitives; it may inspect tags and
// annotated vertices or pass this handle to a placement function.
struct PrefabView {
  Prefab const* prefab;
};

// An immutable copy of one annotated Prefab vertex in Prefab space.
struct PrefabVertexView {
  float x;
  float y;
  std::map<std::string, std::string> metadata;
};

// A read-only, non-owning view of a DefinePrefabs step, letting a script look
// up one of its Prefabs by name without being able to modify the step
// (docs spec #366).
struct DefinePrefabsView {
  DefinePrefabs const* step;
};

// A read-only, non-owning view of a PrimitiveField step, letting a script
// read its authored Primitives without being able to modify the step
// (docs spec #366).
struct PrimitiveFieldView {
  PrimitiveField const* step;
};

// A mutable, borrowed MeshPrimitive plus its editing authority. Keeping the
// proxy alive for the whole handle lifetime lets ids returned by one geometry
// operation be passed directly to the next.
class ScriptMeshPrimitive {
private:
  MeshPrimitive* mPrimitive;
  std::shared_ptr<MeshPrimitiveEditingProxy> mEditing;

public:
  explicit ScriptMeshPrimitive(MeshPrimitive* primitive);

  [[nodiscard]] MeshPrimitive* getPrimitive() const;
  void primitiveTransformChanged();
  [[nodiscard]] bool moveVertexTo(uint32_t vertexId, float x, float y);
  [[nodiscard]] bool moveVertex(
      uint32_t vertexId, float deltaX, float deltaY);
  [[nodiscard]] bool moveEdge(
      uint32_t edgeId, float deltaX, float deltaY);
  [[nodiscard]] bool movePolygon(
      uint32_t polygonId, float deltaX, float deltaY);
  [[nodiscard]] std::optional<uint32_t> splitEdge(uint32_t edgeId);
  [[nodiscard]] std::optional<uint32_t> splitEdge(uint32_t edgeId, float t);
  [[nodiscard]] bool removeVertex(uint32_t vertexId);
  [[nodiscard]] bool removeEdge(uint32_t edgeId);
  [[nodiscard]] bool removePolygon(uint32_t polygonId);
  [[nodiscard]] std::optional<uint32_t> addShell(sol::table const& points);
  [[nodiscard]] std::optional<uint32_t> addHole(
      uint32_t filledPolygonId, sol::table const& points);
  [[nodiscard]] std::optional<uint32_t> addIsland(
      uint32_t holePolygonId, sol::table const& points);
  [[nodiscard]] std::optional<uint32_t> fillHole(uint32_t holePolygonId);
  [[nodiscard]] bool slicePolygon(
      uint32_t polygonId, uint32_t firstVertexId,
      uint32_t secondVertexId);
};

// The borrowed capability object available to a script as `context` while a
// RunScript step executes. It deliberately exposes execution operations, not
// the RunScript object itself or its authored configuration.
class RunScriptContext {
private:
  RunScript const* mStep;
  LayerBuildContext* mBuild;

public:
  RunScriptContext(RunScript const& step, LayerBuildContext& build);

  [[nodiscard]] Primitive* createPrimitive(std::string const& type) const;
  [[nodiscard]] ScriptAudioEmitter addAudioEmitter(
      Primitive* primitive, float x, float y, float heightOffset,
      std::string const& soundId) const;
  [[nodiscard]] std::vector<ScriptAudioEmitter> getAudioEmitters(
      Primitive* primitive) const;
  [[nodiscard]] bool removeAudioEmitter(
      Primitive* primitive, ScriptAudioEmitter const& emitter) const;
  [[nodiscard]] ScriptMeshPrimitive createMeshPrimitive(
      sol::table const& points) const;
  void placePrimitive(Primitive* primitive) const;
  void placeMeshPrimitive(ScriptMeshPrimitive const& primitive) const;
  void placePrefabInstance(
      PrefabView view, int32_t tileX, int32_t tileY, float angle) const;
  [[nodiscard]] std::tuple<int32_t, int32_t> getTile(
      uint32_t gridSize, float x, float y) const;

  [[nodiscard]] DefinePrefabsView findDefinePrefabs(
      std::string const& name) const;
  [[nodiscard]] PrimitiveFieldView findPrimitiveField(
      std::string const& name) const;
  [[nodiscard]] std::vector<PrimitiveView> getBuildPrimitives() const;
  [[nodiscard]] std::tuple<float, float, float, float> getExtents() const;
  [[nodiscard]] std::vector<PrimitiveView> findBuildPrimitivesOverlapping(
      float x, float y, float width, float height) const;
};

// Registers the usertypes a script sees, on the state rather than on any one
// environment: a usertype belongs to the Lua state's registry, while the
// values a script can name are put in its environment by whoever is running
// it. Idempotent, so every execution can call it without cost.
//
// Primitives are bound by borrowed pointer only. Lua never owns a C++ object
// - the step that creates a Primitive owns it - so a script that raises
// halfway through cannot leak or double-free one.
void bindScriptTypes(sol::state& lua);

}  // namespace core
}  // namespace bw
