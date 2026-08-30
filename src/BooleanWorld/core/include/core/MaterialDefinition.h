#pragma once

#include <array>

#include "core/Defines.h"
#include "core/Emboss.h"
#include "core/Serializable.h"

namespace bw {
namespace core {
struct MaterialDefinitionData {
  std::array<float, BW_MATERIAL_PARAMS_MAX> params;
  std::array<float, 3> baseColour;
  // Resolved from a surface's optional Emboss preset, and part of the hash
  // below: two surfaces whose Sub-materials agree but whose presets differ
  // still need their own mesh buckets, because emboss uniforms are per bucket.
  EmbossData emboss;

  uint32_t packedColour() const;
  uint64_t hash(uint32_t materialIndex) const;
};

// Note that emboss is deliberately absent from this type's serialization: a
// MaterialDefinition is only ever built in memory from a Sub-material and an
// optional Emboss preset. Nothing reads or writes the old on-disk shape, so
// there is nothing to migrate.
struct MaterialDefinition : public Serializable {
  MaterialDefinitionData data;

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

}  // namespace core
}  // namespace bw
