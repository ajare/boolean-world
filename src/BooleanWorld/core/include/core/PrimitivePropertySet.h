#pragma once

#include <string>
#include <vector>

#include "core/Elevation.h"
#include "core/LiquidType.h"
#include "core/Serializable.h"
#include "core/SurfaceMaterialReference.h"

namespace bw {
namespace core {
class Primitive;

struct PrimitivePropertySet : public Serializable {
  // The spans are authored; the affine planes are derived against their
  // owning Primitive's fitted elevation bounds. floorZ/ceilingZ remain in the
  // generated property set consumed by the Arrangement.
  ElevationSpan floorSpan{0.0f, 0.0f, 0.0f};
  ElevationSpan ceilingSpan{0.0f, 48.0f, 48.0f};
  Elevation floorZ{0.0f}, ceilingZ{48.0f};

  // Deserialization/compatibility state. False means the legacy affine plane
  // must be converted after an owning Primitive supplies its fitted bounds.
  bool floorSpanAuthored{true};
  bool ceilingSpanAuthored{true};

  // Authored liquid volume scalar, meaningful only on a Primitive whose
  // operation is Union - see ComputeLiquidLevels. Inert (but still stored) on
  // any other operation.
  float liquidLevel{0};

  // Which kind of liquid liquidLevel pours, and so which render material a
  // face wet from this Primitive uses - see LiquidMaterialIndex. Inert (but
  // still stored) wherever liquidLevel itself is.
  LiquidType liquidType{LiquidType::Water};

  // Tagged Surface material references. An empty reference is valid but
  // unresolved. The kind prevents Sub-material ids and qualified Triplanar
  // resource names from being inferred by registry search order.
  SurfaceMaterialReference floorMaterial;
  SurfaceMaterialReference ceilingMaterial;
  SurfaceMaterialReference wallMaterial;

  // Optional stable references into the sole global Embossing catalog. An
  // empty id explicitly means that this surface has no Embossing.
  std::string floorEmbossPresetId;
  std::string ceilingEmbossPresetId;
  std::string wallEmbossPresetId;

public:
  bool childrenModified() const override;

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

}  // namespace core
}  // namespace bw
