#pragma once

#include <array>

#include "core/Defines.h"
#include "core/Serializable.h"

namespace bw {
namespace core {
struct MaterialDefinitionData {
  std::array<float, BW_MATERIAL_PARAMS_MAX> params;
  std::array<float, 3> baseColour;
  uint32_t baseColourUint;

  uint64_t hash(uint32_t materialIndex) const;
};

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
