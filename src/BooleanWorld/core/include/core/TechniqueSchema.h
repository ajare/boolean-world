#pragma once

#include <string>
#include <vector>

#include "core/Serializable.h"

namespace bw {
namespace core {

// One Technique's parameter name, count, and min/max/default bounds - see
// CONTEXT.md's "Technique schema" entry. Authored once per Technique inside
// a ProcMaterialData catalog and shared by every Sub-material selecting
// that Technique.
struct TechniqueParameterSchema {
  std::string name;
  float minimum{0.0f};
  float maximum{0.0f};
  float defaultValue{0.0f};
};

struct TechniqueSchema : public Serializable {
  // The Technique this schema bounds (world_pbr.frag/world_pbr_2d.frag's
  // switch(materialIndex) index, 0-39 - see CONTEXT.md's "Technique" entry).
  uint32_t materialIndex{0};

  std::vector<TechniqueParameterSchema> parameters;

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

}  // namespace core
}  // namespace bw
