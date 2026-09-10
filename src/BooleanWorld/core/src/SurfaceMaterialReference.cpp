#include "core/SurfaceMaterialReference.h"

#include <format>
#include <stdexcept>

namespace bw::core {

SurfaceMaterialKind surfaceMaterialKindFromName(std::string_view name) {
  if (name == "subMaterial") return SurfaceMaterialKind::SubMaterial;
  if (name == "triplanar") return SurfaceMaterialKind::Triplanar;
  throw std::invalid_argument(
      std::format("Unknown Surface material kind '{}'", name));
}

}  // namespace bw::core
