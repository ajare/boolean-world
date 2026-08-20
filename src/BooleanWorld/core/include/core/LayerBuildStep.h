#pragma once

#include <map>
#include <memory>
#include <string>

#include "core/Platform.h"
#include "core/Serializable.h"

namespace bw {
namespace core {

class Layer;
class VertexTransformerObject;

// One step in a Layer's ordered, serialized recipe for producing its
// Primitives. A Layer's Primitives are always derived by re-running its
// enabled steps in order - nothing authors them independently, and the step
// list, not the resulting Primitives, is what a Layer serializes
// (docs/adr/0014). A step's type is fixed for its lifetime.
class BW_API LayerBuildStep : public Serializable {
private:
  bool mEnabled;

private:
  bool childrenModified() const override;

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

  // Constructs a step of the named type through the shared step Registry.
  [[nodiscard]] static LayerBuildStep* instantiate(std::string const& type);

  [[nodiscard]] virtual std::string getType() const = 0;

  // Clones this step. Every Primitive the clone owns is recorded in
  // primitiveMap, keyed by the Primitive it was cloned from, so the owning
  // Layer can remap parent links across the whole step list afterwards.
  [[nodiscard]] virtual LayerBuildStep* copy(
      std::map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const = 0;

  // Adds this step's Primitives to layer, which already holds everything the
  // preceding enabled steps produced. A step may only ever add.
  virtual void execute(Layer& layer) const = 0;

  // These deliberately have no base-class answer: every new step type must
  // say whether the Primitives it produces can be edited directly and whether
  // it can accept newly authored Primitives (docs/adr/0015).
  [[nodiscard]] virtual bool permitsDirectPrimitiveEditing() const = 0;

  [[nodiscard]] virtual bool acceptsNewPrimitives() const = 0;

  void setEnabled(bool enabled);

  [[nodiscard]] bool isEnabled() const;
};

}  // namespace core
}  // namespace bw
