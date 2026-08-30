#pragma once

#include <string>

#include "core/Emboss.h"
#include "core/Serializable.h"

namespace bw::core {

// A named reusable Embossing definition. Primitive surfaces retain `id`, so
// changing displayName never invalidates an authored reference.
struct EmbossPreset : public Serializable {
  std::string id;
  std::string displayName;
  EmbossData emboss;

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer,
                     SerializationWorkData& workData) const override;
  bool deserializeImpl(std::shared_ptr<Serializer> serializer,
                       SerializationWorkData& workData) override;
};

}  // namespace bw::core
