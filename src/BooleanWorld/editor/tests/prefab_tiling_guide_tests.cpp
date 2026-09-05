#include <iostream>
#include <stdexcept>

#include <core/MeshPrimitive.h>

#include "PrefabPlacementPreview.h"
#include "PrefabTilingGuide.h"

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::Vertex vertex(float x, float y) {
  return bw::core::Vertex{{x, y}};
}

void placementPreviewMovesEveryPrefabContourToTheTargetTile() {
  bw::core::DefinePrefabs definitions;
  auto* prefab = definitions.addPrefab("Courtyard");
  definitions.setSelectedPrefab(prefab);
  auto* mesh = bw::core::MeshPrimitive::fromComplexPolygons(
      bw::core::Primitive::Operation::Union,
      {{{vertex(-4.0f, -4.0f), vertex(4.0f, -4.0f),
          vertex(4.0f, 4.0f), vertex(-4.0f, 4.0f)},
         {vertex(-2.0f, -2.0f), vertex(-2.0f, 2.0f),
          vertex(2.0f, 2.0f), vertex(2.0f, -2.0f)}}});
  definitions.adoptPrimitive(mesh);
  auto* marker = bw::core::MeshPrimitive::fromComplexPolygons(
      bw::core::Primitive::Operation::Union,
      {{{vertex(9.0f, -1.0f), vertex(11.0f, -1.0f),
          vertex(10.0f, 1.0f)}}});
  definitions.adoptPrimitive(marker);

  auto const outlines = editor::prefabPlacementOutlines(
      *prefab,
      {bw::core::PrefabTileSize::Size64, 1, -1});

  std::vector<editor::PrefabPlacementOutline> const expected{
      {{92.0f, -36.0f}, {100.0f, -36.0f},
       {100.0f, -28.0f}, {92.0f, -28.0f}},
      {{98.0f, -34.0f}, {98.0f, -30.0f},
       {94.0f, -30.0f}, {94.0f, -34.0f}},
      {{105.0f, -33.0f}, {107.0f, -33.0f},
       {106.0f, -31.0f}}};
  require(outlines == expected,
          "the placement preview did not preserve and translate every Prefab contour");

  auto const rotated = editor::prefabPlacementOutlines(
      *prefab,
      {bw::core::PrefabTileSize::Size64, 1, -1}, 90.0f);
  require(rotated[0] == editor::PrefabPlacementOutline{
                            {92.0f, -28.0f}, {92.0f, -36.0f},
                            {100.0f, -36.0f}, {100.0f, -28.0f}},
          "the placement preview did not apply the pending clockwise rotation");
}

void squareGuideUsesSizeAsItsEdgeLengthAndIsCentredOnTheOrigin() {
  auto const outline = editor::prefabTilingOutline(
      bw::core::PrefabTilingType::Square, 64.0f);

  require(outline.size() == 4, "a square tiling guide did not have four vertices");
  require(outline[0] == wp::Vector2{-32.0f, -32.0f} &&
              outline[1] == wp::Vector2{32.0f, -32.0f} &&
              outline[2] == wp::Vector2{32.0f, 32.0f} &&
              outline[3] == wp::Vector2{-32.0f, 32.0f},
          "a size-64 square tiling guide was not centred and bounded by +/-32");
}

}  // namespace

int main() {
  try {
    placementPreviewMovesEveryPrefabContourToTheTargetTile();
    squareGuideUsesSizeAsItsEdgeLengthAndIsCentredOnTheOrigin();
    std::cout << "Prefab tiling guide tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
