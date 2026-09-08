#include <iostream>
#include <stdexcept>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/MeshPrimitive.h>
#include <core/PrimitivePropertySet.h>
#include <core/World.h>

namespace {

using bw::core::ComplexPolygon;
using bw::core::MeshPrimitive;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ComplexPolygon rectangle(float left, float bottom, float right, float top) {
  return {{{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}}};
}

MeshPrimitive* makeRectangle(
    Primitive::Operation operation, float left, float bottom, float right,
    float top, float floorZ, float ceilingZ) {
  auto* primitive = MeshPrimitive::fromComplexPolygons(
      operation, {rectangle(left, bottom, right, top)});
  PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  primitive->setProperties(properties);
  return primitive;
}

// Builds the exact scoped-primitive-list construction OpenPreview3D uses
// (ticket #269): a real ArrangementWorldData built once from a fixed
// Primitive list, using the same extents/grid-cell-size source as
// DynamicWorldDataGenerator's constructor.
void generatedWorldDataSamplesElevationPlanesAtTheRequestedPosition() {
  bw::core::World world(20.0f, 2.0f);
  auto* primitive = makeRectangle(
      Primitive::Operation::Union, 0.0f, 0.0f, 10.0f, 10.0f, 3.0f, 20.0f);
  auto properties = primitive->getProperties();
  properties.floorZ.gradient = {0.5f, -0.25f};
  properties.ceilingZ.gradient = {0.0f, 0.125f};
  primitive->setProperties(properties);
  world.addPrimitive(primitive);

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(std::vector<Primitive*>{primitive});
  bw::core::ArrangementWorldData worldData(
      generator.getWorldData(), world.getExtents(), 2.0f);

  auto sample = worldData.getSurfaceSample({2.0f, 4.0f});
  require(sample.has_value(), "a solid face did not produce a surface sample");
  require(std::abs(sample->floorElevation - 1.75f) < 0.0001f &&
              std::abs(sample->ceilingElevation - 19.875f) < 0.0001f,
          "the surface sample did not evaluate both Elevation planes locally");
  auto normalLength = std::sqrt(1.3125f);
  require(std::abs(sample->floorNormal[0] + 0.5f / normalLength) < 0.0001f &&
              std::abs(sample->floorNormal[1] - 0.25f / normalLength) < 0.0001f &&
              std::abs(sample->floorNormal[2] - 1.0f / normalLength) < 0.0001f &&
              sample->ceilingNormal[2] < 0.0f,
          "the surface sample did not expose outward plane normals");
  require(sample->face ==
                  &worldData.getArrangement().faces[sample->faceIndex] &&
              sample->face->solid,
          "the surface sample did not identify its containing Arrangement face");
  require(std::abs(worldData.getFloorHeight({2.0f, 4.0f}) -
                   sample->floorElevation) < 0.0001f &&
              std::abs(worldData.getCeilingHeight({2.0f, 4.0f}) -
                       sample->ceilingElevation) < 0.0001f,
          "flat-compatible height queries disagreed with the authoritative sample");
  require(!worldData.getSurfaceSample({-5.0f, -5.0f}),
          "a position outside generated geometry produced a surface sample");
}

void aPreviewArrangementReflectsTheNeighborAwareSteppedHeights() {
  bw::core::World world(20.0f, 2.0f);
  auto* base = makeRectangle(
      Primitive::Operation::Union, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 48.0f);
  auto* raised = makeRectangle(
      Primitive::Operation::Union, 4.0f, 3.0f, 6.0f, 7.0f, 8.0f, 32.0f);
  raised->setPriority(1);
  world.addPrimitive(base);
  world.addPrimitive(raised);

  std::vector<Primitive*> primitives{base, raised};

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(primitives);
  bw::core::ArrangementWorldData worldData(
      generator.getWorldData(), world.getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX));

  // A per-Primitive extrusion (PrimitivePreviewGeometry, the thing this
  // ticket series replaces) would instead draw the raised Primitive's own
  // wall spanning its full 8..32 range, with no split at the shared
  // boundary. A real Arrangement instead produces the two real step faces:
  // a FloorStep from the lower floor up to the higher one (0..8), and a
  // CeilingStep from the lower ceiling up to the higher one (32..48).
  bool sawFloorStep = false, sawCeilingStep = false, sawNaiveOwnRange = false;
  for (auto const& wall : worldData.getWalls()) {
    if (wall.kind == bw::core::arr::ArrangementWallKind::FloorStep &&
        std::abs(wall.minZ - 0.0f) < 0.01f &&
        std::abs(wall.maxZ - 8.0f) < 0.01f) {
      sawFloorStep = true;
    }
    if (wall.kind == bw::core::arr::ArrangementWallKind::CeilingStep &&
        std::abs(wall.minZ - 32.0f) < 0.01f &&
        std::abs(wall.maxZ - 48.0f) < 0.01f) {
      sawCeilingStep = true;
    }
    if (std::abs(wall.minZ - 8.0f) < 0.01f &&
        std::abs(wall.maxZ - 32.0f) < 0.01f) {
      sawNaiveOwnRange = true;
    }
  }
  require(
      sawFloorStep,
      "the previewed Arrangement did not produce a FloorStep wall spanning "
      "the real neighbor-aware floor step (0..8), not either Primitive's own "
      "Z range");
  require(
      sawCeilingStep,
      "the previewed Arrangement did not produce a CeilingStep wall spanning "
      "the real neighbor-aware ceiling step (32..48), not either Primitive's "
      "own Z range");
  require(
      !sawNaiveOwnRange,
      "a wall reported the raised Primitive's own unclipped 8..32 Z range, "
      "as an independent per-Primitive extrusion would, instead of the "
      "neighbor-aware step split");
}

}  // namespace

int main() {
  try {
    generatedWorldDataSamplesElevationPlanesAtTheRequestedPosition();
    aPreviewArrangementReflectsTheNeighborAwareSteppedHeights();
    std::cout
        << "A preview's Arrangement build reflects neighbor-aware stepped heights\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
