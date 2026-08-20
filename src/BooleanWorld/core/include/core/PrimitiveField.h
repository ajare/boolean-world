#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/LayerBuildStep.h"
#include "core/Platform.h"
#include "core/Primitive.h"

namespace bw {
namespace core {

class Layer;

// The basic LayerBuildStep: an embedded, literal list of Primitive
// definitions that it owns and adds verbatim to the Layer being built
// (docs/adr/0014). Unrelated to the Voronoi-based PrimitiveFieldLayout
// placement feature, whose name this coincidentally echoes.
class BW_API PrimitiveField : public LayerBuildStep {
private:
  // Owned. The Layer these are handed to holds them only as a derived cache.
  std::vector<Primitive*> mPrimitives;

private:
  bool childrenModified() const override;

  void serializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  PrimitiveField();

  PrimitiveField(PrimitiveField const& other) = delete;

  PrimitiveField& operator=(PrimitiveField const& other) = delete;

  ~PrimitiveField() override;

  [[nodiscard]] std::string getType() const override;

  [[nodiscard]] LayerBuildStep* copy(
      std::map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const override;

  void execute(Layer& layer) const override;

  [[nodiscard]] bool permitsDirectPrimitiveEditing() const override;

  [[nodiscard]] bool acceptsNewPrimitives() const override;

  // Takes ownership of primitive and returns its index in this step's list.
  uint32_t addPrimitive(Primitive* primitive);

  void removePrimitive(Primitive* primitive);

  // Like removePrimitive, but hands the Primitive back instead of destroying
  // it - the caller now owns it.
  [[nodiscard]] Primitive* releasePrimitive(Primitive* primitive);

  // Destroys oldPrimitive and takes ownership of newPrimitive in its place.
  // Replacing a Primitive with itself is a no-op.
  void replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive);

  [[nodiscard]] bool contains(Primitive const* primitive) const;

  [[nodiscard]] uint32_t getNumPrimitives() const;

  [[nodiscard]] Primitive* getPrimitive(uint32_t index) const;

  [[nodiscard]] std::vector<Primitive*> const& getPrimitives() const;

  void clear();
};

}  // namespace core
}  // namespace bw
