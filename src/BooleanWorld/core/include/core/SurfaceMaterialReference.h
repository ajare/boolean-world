#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace bw::core {

// The material family selected by one authored World surface. Keep this tag
// explicit: Sub-material ids and qualified Triplanar resource names occupy
// different identity domains.
enum class SurfaceMaterialKind {
  SubMaterial,
  Triplanar,
};

[[nodiscard]] constexpr std::string_view surfaceMaterialKindName(
    SurfaceMaterialKind kind) {
  switch (kind) {
    case SurfaceMaterialKind::SubMaterial: return "subMaterial";
    case SurfaceMaterialKind::Triplanar: return "triplanar";
  }
  return "";
}

[[nodiscard]] SurfaceMaterialKind surfaceMaterialKindFromName(
    std::string_view name);

struct SurfaceMaterialReference {
  SurfaceMaterialKind kind{SurfaceMaterialKind::SubMaterial};
  std::string reference;

  SurfaceMaterialReference() = default;
  SurfaceMaterialReference(SurfaceMaterialKind kind, std::string value)
      : kind(kind), reference(std::move(value)) {
  }

  [[nodiscard]] static SurfaceMaterialReference subMaterial(
      std::string value) {
    return {SurfaceMaterialKind::SubMaterial, std::move(value)};
  }

  [[nodiscard]] static SurfaceMaterialReference triplanar(std::string value) {
    return {SurfaceMaterialKind::Triplanar, std::move(value)};
  }

  bool operator==(SurfaceMaterialReference const&) const = default;
};

}  // namespace bw::core
