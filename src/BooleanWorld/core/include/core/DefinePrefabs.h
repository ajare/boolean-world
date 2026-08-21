#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/LayerBuildStep.h"
#include "core/Platform.h"
#include "core/Primitive.h"

namespace bw {
namespace core {

// The tiling frame shared by every Prefab in a DefinePrefabs step. Additional
// tiling types can be added without changing Prefab itself.
enum class PrefabTilingType : uint32_t {
  Square = 0,
};

// The ordered rotations a Prefab instance may use for this tiling type.
// This core lookup deliberately does not depend on the editor-only tiling
// guide definitions.
[[nodiscard]] std::span<float const> prefabTilingRotationAngles(PrefabTilingType type);

// A named, stably identified collection of Primitives authored as a unit.
// Prefab deliberately is not Serializable: DefinePrefabs owns persistence and
// modification aggregation for the complete collection.
class BW_API Prefab {
private:
  uint32_t mId;
  std::string mName;
  std::vector<Primitive*> mPrimitives;

  Prefab(uint32_t id, std::string const& name);

  [[nodiscard]] Prefab* copy(
      std::map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const;

  uint32_t adoptPrimitive(Primitive* primitive);
  void replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive);
  [[nodiscard]] bool ownsPrimitive(Primitive const* primitive) const;
  void clear();

  friend class DefinePrefabs;

public:
  Prefab(Prefab const&) = delete;
  Prefab& operator=(Prefab const&) = delete;
  ~Prefab();

  [[nodiscard]] uint32_t getId() const;
  [[nodiscard]] std::string const& getName() const;
  [[nodiscard]] uint32_t getNumPrimitives() const;
  [[nodiscard]] Primitive* getPrimitive(uint32_t index) const;
  [[nodiscard]] std::vector<Primitive*> const& getPrimitives() const;
};

// Defines reusable Prefabs without contributing world geometry. Only the
// transiently selected Prefab is emitted into the Layer's derived authoring
// cache, and that output never participates in later build steps (ADR-0017).
class BW_API DefinePrefabs : public LayerBuildStep {
private:
  std::vector<Prefab*> mPrefabs;
  uint32_t mNextPrefabId;
  PrefabTilingType mTilingType;
  float mSize;
  Prefab* mSelectedPrefab;

  bool childrenModified() const override;
  void serializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;
  bool deserializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  void clear();

public:
  DefinePrefabs();
  DefinePrefabs(DefinePrefabs const&) = delete;
  DefinePrefabs& operator=(DefinePrefabs const&) = delete;
  ~DefinePrefabs() override;

  [[nodiscard]] std::string getType() const override;
  [[nodiscard]] bool mayBeFirstStep() const override;
  [[nodiscard]] LayerBuildStep* copy(
      std::map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const override;
  void execute(LayerBuildContext& context) const override;
  [[nodiscard]] bool primitivesParticipateInBuild() const override;
  [[nodiscard]] bool permitsDirectPrimitiveEditing() const override;
  [[nodiscard]] bool acceptsNewPrimitives() const override;
  uint32_t adoptPrimitive(Primitive* primitive) override;
  void replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive) override;
  [[nodiscard]] bool ownsPrimitive(Primitive const* primitive) const override;

  // Returns the created Prefab. Names are display text and need not be unique.
  Prefab* addPrefab(std::string const& name);
  void removePrefab(Prefab* prefab, bool failIfNotFound = true);
  void removePrefab(uint32_t index);
  void setPrefabName(Prefab* prefab, std::string const& name);

  [[nodiscard]] uint32_t getNumPrefabs() const;
  [[nodiscard]] Prefab* getPrefab(uint32_t index) const;
  [[nodiscard]] Prefab* findPrefabById(uint32_t id) const;
  [[nodiscard]] std::vector<Prefab*> const& getPrefabs() const;

  // Selection is editor focus only: it is never serialized or copied.
  void setSelectedPrefab(Prefab* prefab);
  void setSelectedPrefabIndex(uint32_t index);
  void clearSelectedPrefab();
  [[nodiscard]] Prefab* getSelectedPrefab() const;
  [[nodiscard]] uint32_t getSelectedPrefabIndex() const;

  void setTilingType(PrefabTilingType type);
  [[nodiscard]] PrefabTilingType getTilingType() const;
  void setSize(float size);
  [[nodiscard]] float getSize() const;
};

}  // namespace core
}  // namespace bw
