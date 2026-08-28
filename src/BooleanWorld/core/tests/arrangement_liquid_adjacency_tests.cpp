#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/PrimitivePropertySet.h>

namespace {
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;
using bw::core::arr::ToFixedPointCoordinate;
using bw::core::arr::LiquidAdjacency;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

Contour rectangle(double minX, double minY, double maxX, double maxY) {
  auto fp = [](double v) { return ToFixedPointCoordinate(v); };
  return {{fp(minX), fp(minY)}, {fp(maxX), fp(minY)},
          {fp(maxX), fp(maxY)}, {fp(minX), fp(maxY)}};
}

PrimitivePropertySet heights(float floorZ, float ceilingZ) {
  PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  return properties;
}

bool isAdjacent(
    std::vector<LiquidAdjacency> const& adjacency, uint32_t a, uint32_t b) {
  auto lo = std::min(a, b);
  auto hi = std::max(a, b);
  return std::ranges::any_of(adjacency, [&](auto const& pair) {
    return pair.face0 == lo && pair.face1 == hi;
  });
}

bool isDrain(
    std::vector<LiquidAdjacency> const& adjacency, uint32_t a, uint32_t b) {
  auto lo = std::min(a, b);
  auto hi = std::max(a, b);
  for (auto const& pair : adjacency) {
    if (pair.face0 == lo && pair.face1 == hi) {
      return pair.drain;
    }
  }
  return false;
}

// Two ordinary solid rooms - each just a Union primitive, the same as any
// room in an authored World - sharing an edge with nothing carved between
// them: an open doorway with no remaining wall.
void openDoorwayFacesAreLiquidAdjacent() {
  auto roomA = ArrangementPrimitive{
      {rectangle(0, 0, 100, 100)}, Primitive::Operation::Union,
      Primitive::FillRule::NonZero, 0, 1, heights(0.0f, 48.0f)};
  auto roomB = ArrangementPrimitive{
      {rectangle(100, 0, 200, 100)}, Primitive::Operation::Union,
      Primitive::FillRule::NonZero, 1, 2, heights(0.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({roomA, roomB});

  int roomAFace = -1, roomBFace = -1;
  for (uint32_t i = 1; i < arrangement->faces.size(); ++i) {
    auto const& face = arrangement->faces[i];
    require(face.solid, "an ordinary Union room should be solid");
    if (face.primitiveIndex == 1) roomAFace = int(i);
    if (face.primitiveIndex == 2) roomBFace = int(i);
  }
  require(roomAFace >= 0 && roomBFace >= 0, "both rooms should be present as their own faces");

  auto adjacency = bw::core::arr::BuildLiquidAdjacency(*arrangement);
  require(
      isAdjacent(adjacency, uint32_t(roomAFace), uint32_t(roomBFace)),
      "two open, same-height rooms sharing a boundary should be liquid-adjacent");
  require(
      !isDrain(adjacency, uint32_t(roomAFace), uint32_t(roomBFace)),
      "an ordinary interior adjacency must not be reported as the exterior drain");
}

// Same footprint, but the two rooms sit at different heights with zero
// shared headroom - a sealed boundary despite having no solid material
// between them.
void zeroClearanceRoomsAreNotLiquidAdjacent() {
  auto roomA = ArrangementPrimitive{
      {rectangle(0, 0, 100, 100)}, Primitive::Operation::Union,
      Primitive::FillRule::NonZero, 0, 1, heights(0.0f, 24.0f)};
  auto roomB = ArrangementPrimitive{
      {rectangle(100, 0, 200, 100)}, Primitive::Operation::Union,
      Primitive::FillRule::NonZero, 1, 2, heights(24.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({roomA, roomB});

  int roomAFace = -1, roomBFace = -1;
  for (uint32_t i = 1; i < arrangement->faces.size(); ++i) {
    auto const& face = arrangement->faces[i];
    if (face.primitiveIndex == 1) roomAFace = int(i);
    if (face.primitiveIndex == 2) roomBFace = int(i);
  }
  require(roomAFace >= 0 && roomBFace >= 0, "both rooms should be present as their own faces");

  auto adjacency = bw::core::arr::BuildLiquidAdjacency(*arrangement);
  require(
      !isAdjacent(adjacency, uint32_t(roomAFace), uint32_t(roomBFace)),
      "rooms whose shared boundary has zero clearance must not be liquid-adjacent");
}

// A lone solid room with nothing surrounding it shares its whole boundary
// directly with the Arrangement's own unbounded exterior face - there is no
// solid material beyond it at all - so it must be reported as adjacent to
// that face, distinguishable as the permanent drain.
void roomsTouchingTheOuterBoundaryDrainToTheExteriorFace() {
  auto room = ArrangementPrimitive{
      {rectangle(0, 0, 100, 100)}, Primitive::Operation::Union,
      Primitive::FillRule::NonZero, 0, 1, heights(0.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({room});

  int roomFace = -1;
  for (uint32_t i = 1; i < arrangement->faces.size(); ++i) {
    auto const& face = arrangement->faces[i];
    if (face.primitiveIndex == 1 && face.solid) roomFace = int(i);
  }
  require(roomFace >= 0, "the room should be present as its own solid face");

  auto adjacency = bw::core::arr::BuildLiquidAdjacency(*arrangement);
  require(
      isAdjacent(adjacency, 0, uint32_t(roomFace)),
      "a room bordering the Arrangement's own outer boundary must be liquid-adjacent to the exterior face");
  require(
      isDrain(adjacency, 0, uint32_t(roomFace)),
      "an exterior adjacency must be reported as the permanent drain");
}

}  // namespace

int main() {
  try {
    openDoorwayFacesAreLiquidAdjacent();
    zeroClearanceRoomsAreNotLiquidAdjacent();
    roomsTouchingTheOuterBoundaryDrainToTheExteriorFace();
    std::cout << "The liquid-adjacency relation reflects clearance and the exterior drain\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
