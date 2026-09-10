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

  // Expand-phase C++ compatibility for callers that still treat a surface as
  // a Sub-material id. These helpers are deliberately local to this value type
  // and are removed by #447 after those callers consume kind explicitly.
  SurfaceMaterialReference(std::string value)
      : reference(std::move(value)) {
  }
  SurfaceMaterialReference& operator=(std::string value) {
    kind = SurfaceMaterialKind::SubMaterial;
    reference = std::move(value);
    return *this;
  }
  SurfaceMaterialReference& operator=(char const* value) {
    return *this = std::string(value);
  }
  [[nodiscard]] bool empty() const {
    return reference.empty();
  }
  operator std::string const&() const {
    return reference;
  }

  [[nodiscard]] static SurfaceMaterialReference subMaterial(
      std::string value) {
    return {SurfaceMaterialKind::SubMaterial, std::move(value)};
  }

  [[nodiscard]] static SurfaceMaterialReference triplanar(std::string value) {
    return {SurfaceMaterialKind::Triplanar, std::move(value)};
  }

  bool operator==(SurfaceMaterialReference const&) const = default;
  bool operator==(std::string const& value) const {
    return reference == value;
  }
  bool operator==(std::string_view value) const {
    return reference == value;
  }
  bool operator==(char const* value) const {
    return reference == value;
  }
};

}  // namespace bw::core
