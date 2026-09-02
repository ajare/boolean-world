#pragma once

#include <functional>
#include <map>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Platform.h"
#include "core/Serializable.h"

namespace bw {
namespace core {

class Layer;
class LayerBuildStep;
class Primitive;
class VertexTransformerObject;
template <typename T>
class Registry;

// The only view of a Layer that a build step receives while executing. It
// exposes prior Primitives that participate in the build, never the raw
// derived cache, and records appended output against the executing step.
class BW_API LayerBuildContext {
private:
  Layer& mLayer;
  LayerBuildStep const* mStep;
  uint32_t mStepIndex;
  std::vector<Primitive*> const& mBuildPrimitives;

  LayerBuildContext(
      Layer& layer,
      LayerBuildStep const* step,
      uint32_t stepIndex,
      std::vector<Primitive*> const& buildPrimitives);

  friend class Layer;

public:
  [[nodiscard]] std::vector<Primitive*> const& getBuildPrimitives() const;
  [[nodiscard]] Layer& getLayer() const;

  // The default ordering is one phase using the Primitive's authored
  // priority. Composite steps may provide local phases while retaining that
  // priority as the ordering within a phase.
  uint32_t appendPrimitive(Primitive* primitive);
  uint32_t appendPrimitive(
      Primitive* primitive, uint8_t phase, uint8_t relativePriority);
};

// One step in a Layer's ordered, serialized recipe for producing its
// Primitives. A Layer's Primitives are always derived by re-running its
// enabled steps in order - nothing authors them independently, and the step
// list, not the resulting Primitives, is what a Layer serializes
// (docs/adr/0014). A step's type is fixed for its lifetime.
class BW_API LayerBuildStep : public Serializable {
private:
  uint32_t mId;
  bool mEnabled;

  // Whether this step's execute() threw during the Layer's most recent
  // rebuild, and what it said. mutable because execute() is const but a
  // failure is recorded from inside it (docs/adr/0039), following the
  // precedent set by PrefabField's mutable built-Primitive storage.
  mutable bool mFailed;
  mutable std::string mFailureMessage;

  void setId(uint32_t id);

  // Called by Layer at the execute() boundary: clearFailure() before every
  // attempt, recordFailure() if that attempt throws.
  void clearFailure() const;
  void recordFailure(std::string message) const;

  friend class Layer;

private:
  bool childrenModified() const override;

  [[nodiscard]] static Registry<LayerBuildStep>& registry();

  // The step's own arguments, written into the map the Layer opens for it.
  // The enabled flag is handled by the base class, so subclasses never write
  // it themselves.
  virtual void serializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const = 0;

  virtual bool deserializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) = 0;

protected:
  void copyFrom(LayerBuildStep const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const final;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) final;

public:
  LayerBuildStep();

  using Factory = std::function<LayerBuildStep*()>;

  // The type names held by the shared step Registry.
  [[nodiscard]] static std::vector<std::string> getRegisteredTypes();

  // Constructs a step of the named type through the shared step Registry.
  [[nodiscard]] static LayerBuildStep* instantiate(std::string const& type);

  // Registers a step type by name with a creating factory. core names no
  // concrete step type in its own registry (docs/adr/0038); a host calls
  // this explicitly during startup for every step type it wants Worlds to
  // be able to deserialize. Not done through a static initialiser: the
  // linker discards one from a static library when nothing else references
  // its translation unit.
  static void registerType(std::string const& type, Factory factory);

  // Registers the step types core itself defines: DefinePrefabs, PrefabField
  // and PrimitiveField. Every host that deserializes Worlds must call this.
  static void registerCoreTypes();

  [[nodiscard]] virtual std::string getType() const = 0;

  // Stable within the owning Layer's lifetime. Unlike a step's position in
  // the recipe, this does not change when other steps are moved or removed.
  [[nodiscard]] uint32_t getId() const;

  // Whether this step may occupy the Layer's reserved first position.
  [[nodiscard]] virtual bool mayBeFirstStep() const = 0;

  // Clones this step. Every Primitive the clone owns is recorded in
  // primitiveMap, keyed by the Primitive it was cloned from, so the owning
  // Layer can remap parent links across the whole step list afterwards.
  [[nodiscard]] virtual LayerBuildStep* copy(
      std::map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const = 0;

  // Reads only the build-participating Primitives produced by preceding
  // enabled steps and may append new derived Primitives through context.
  virtual void execute(LayerBuildContext& context) const = 0;

  // Whether Primitives this step produces participate in later steps' build
  // input. This is deliberately explicit so a defining step can expose its
  // Primitives to authoring without leaking them into the build.
  [[nodiscard]] virtual bool primitivesParticipateInBuild() const = 0;

  // These deliberately have no base-class answer: every new step type must
  // say whether the Primitives it produces can be edited directly and whether
  // it can accept newly authored Primitives (docs/adr/0015).
  [[nodiscard]] virtual bool permitsDirectPrimitiveEditing() const = 0;

  [[nodiscard]] virtual bool acceptsNewPrimitives() const = 0;

  // Storage operations are virtual because more than one kind of step may
  // own Primitives. Layer uses these only to maintain its derived cache.
  virtual uint32_t adoptPrimitive(Primitive* primitive) = 0;

  // Replaces one owned Primitive. A null replacement removes it.
  virtual void replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive) = 0;

  // Gives up ownership of one owned Primitive without destroying it, so a
  // caller can re-home it in another step. Deliberately distinct from
  // replacePrimitive(primitive, nullptr), which destroys: a step that cannot
  // hand its output out must say so by throwing rather than silently
  // leaking or deleting.
  virtual void releasePrimitive(Primitive* primitive) = 0;

  [[nodiscard]] virtual bool ownsPrimitive(Primitive const* primitive) const = 0;

  // Resource names this step references directly (docs/adr/0038), collected
  // by World into the serialized dependentResources header. The default
  // answer is none; a step that owns resource references overrides this
  // rather than World knowing its concrete type.
  [[nodiscard]] virtual std::vector<std::string> collectDependentResourceNames() const;

  void setEnabled(bool enabled);

  [[nodiscard]] bool isEnabled() const;

  // Whether this step's execute() threw during the Layer's most recent
  // rebuild (docs/adr/0039). False for a step that ran and produced
  // nothing, or that was not reached because an earlier step in the same
  // rebuild failed - both are distinct from having failed itself.
  [[nodiscard]] bool hasFailed() const;

  // The message from this step's most recent failure. Empty when
  // hasFailed() is false.
  [[nodiscard]] std::string const& getFailureMessage() const;
};

}  // namespace core
}  // namespace bw
