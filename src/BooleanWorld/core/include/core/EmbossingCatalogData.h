#pragma once

#include <string>
#include <vector>

#include "core/EmbossPreset.h"
#include "core/Serializable.h"

namespace bw::core {

// Data payload of the sole global EmbossingCatalog Resource.
struct EmbossingCatalogData : public Serializable {
  std::vector<EmbossPreset> presets;

  [[nodiscard]] EmbossPreset const* findPreset(std::string const& id) const;

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer,
                     SerializationWorkData& workData) const override;
  bool deserializeImpl(std::shared_ptr<Serializer> serializer,
                       SerializationWorkData& workData) override;
};

}  // namespace bw::core
