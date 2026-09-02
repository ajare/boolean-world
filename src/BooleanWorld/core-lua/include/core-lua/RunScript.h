#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <core/LayerBuildStep.h>
#include <core/Primitive.h>

#include "core-lua/ScriptRuntime.h"

namespace bw {
namespace core {

class Prefab;

// The LayerBuildStep that runs a Lua script to produce its Primitives. It is
// a placing step, never a defining one: its output participates in the build
// and folds in recipe order like any other step's (docs/adr/0014).
//
// It owns what its script creates, in storage cleared and refilled by every
// execute() - the PrefabField precedent - and hands the script nothing but
// borrowed handles. A script therefore never owns a Primitive, so a script
// that raises halfway through can neither leak one nor free one twice; a
// created Primitive the script never places costs one wasted allocation.
class RunScript final : public LayerBuildStep {
private:
  // Not owned. The host's runtime, injected through the registration factory
  // rather than reached for through a singleton, so each test can own its own.
  ScriptRuntime* mRuntime;

  std::string mScriptName;

  // Re-applied to math.random at the start of every execute(), so a scatter
  // reproduces exactly and changing the arrangement is an authored edit
  // rather than a side effect of rebuilding (docs/adr/0040).
  uint64_t mSeed = 0;

  // Cleared and refilled by every execute(). mutable because execute() is
  // const, following PrefabField.
  mutable std::vector<std::unique_ptr<Primitive>> mBuiltPrimitives;

  // Which of the above the script has placed into the build this execute().
  mutable std::vector<Primitive const*> mPlacedPrimitives;

  void serializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  // Allocates a Primitive of the named type directly into this step's
  // storage and returns a borrowed handle to it.
  [[nodiscard]] Primitive* createPrimitive(std::string const& type) const;

  void placePrimitive(LayerBuildContext& context, Primitive* primitive) const;

  // Clones prefab's Primitives into this step's storage, offset by (x, y) and
  // rotated by angle (degrees, the same convention PrefabField's tiling
  // angles use), preserving their parent links among each other, and appends
  // the clones to context. The Prefab's own Primitives are never touched -
  // instances are copies (docs spec #366).
  void placePrefabInstance(
      LayerBuildContext& context, Prefab const* prefab, float x, float y, float angle) const;

public:
  explicit RunScript(ScriptRuntime& runtime);

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

  void releasePrimitive(Primitive* primitive) override;

  [[nodiscard]] bool ownsPrimitive(Primitive const* primitive) const override;

  // The name the script was loaded into the ScriptRuntime under. Serializing
  // it as a resource name, along with the seed and the extra-resources list,
  // is a later ticket.
  void setScriptName(std::string const& name);

  [[nodiscard]] std::string const& getScriptName() const;

  // The in-memory seed re-applied to math.random at the start of every
  // execute(). Serializing it is a later ticket.
  void setSeed(uint64_t seed);

  [[nodiscard]] uint64_t getSeed() const;

  [[nodiscard]] ScriptRuntime& getRuntime() const;
};

}  // namespace core
}  // namespace bw
