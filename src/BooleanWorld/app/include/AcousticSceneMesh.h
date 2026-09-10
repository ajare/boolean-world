#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <core/SurfaceMaterialReference.h>

#include "AcousticCatalog.h"

namespace bw::core {
class ArrangementWorldData;
}

namespace bw::app {

// Steam Audio-space geometry: authored (x, y) becomes (x, elevation, -y).
// This representation deliberately contains no Steam Audio types so export
// correctness can be tested without loading the plugin DLL.
struct AcousticSceneVertex {
  float x{};
  float y{};
  float z{};

  bool operator==(AcousticSceneVertex const&) const = default;
};

struct AcousticSceneTriangle {
  std::array<uint32_t, 3> vertices{};
  uint32_t materialIndex{};

  bool operator==(AcousticSceneTriangle const&) const = default;
};

struct AcousticSceneMesh {
  std::vector<AcousticSceneVertex> vertices;
  std::vector<AcousticSceneTriangle> triangles;
  // Contains exactly the resolved presets used by triangles, de-duplicated by
  // stable preset id. Triangle materialIndex values index this array.
  std::vector<AcousticPreset> materials;
};

// The callback resolves a generated Surface material reference to its Acoustic
// preset. Keeping the family tag prevents a Triplanar resource name from being
// inferred as a Sub-material id. AcousticPresetResolver supplies this seam at
// runtime; tests can use a small in-memory catalog without the resource system.
using AcousticMaterialResolver = std::function<AcousticPreset const&(
    core::SurfaceMaterialReference const&)>;

[[nodiscard]] AcousticSceneMesh ExportAcousticSceneMesh(
    core::ArrangementWorldData const& world,
    AcousticMaterialResolver const& resolveMaterial);

}  // namespace bw::app
