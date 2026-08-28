#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/PrimitivePropertySet.h>

namespace {
using bw::core::ArrangementWorldData;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;
using bw::core::arr::ToFixedPointCoordinate;

constexpr double Epsilon = 0.001;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(double actual, double expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon, message);
}

Contour rectangle(double minX, double minY, double maxX, double maxY) {
  auto fp = [](double v) { return ToFixedPointCoordinate(v); };
  return {{fp(minX), fp(minY)}, {fp(maxX), fp(minY)},
          {fp(maxX), fp(maxY)}, {fp(minX), fp(maxY)}};
}

PrimitivePropertySet properties(float floorZ, float ceilingZ, float waterLevel) {
  PrimitivePropertySet result;
  result.floorZ = floorZ;
  result.ceilingZ = ceilingZ;
  result.waterLevel = waterLevel;
  return result;
}

// rawArea stands in for Primitive::getArea(), which these fixed-point-contour
// arrangement tests have no real Primitive to call - the caller supplies the
// same rectangle area it used to build the contour.
ArrangementPrimitive rectanglePrimitive(
    Contour contour, Primitive::Operation operation, uint8_t priority,
    uint32_t primitiveIndex, PrimitivePropertySet const& props, double rawArea) {
  ArrangementPrimitive result{
      {std::move(contour)}, operation, Primitive::FillRule::NonZero,
      priority, primitiveIndex, props};
  result.rawArea = rawArea;
  return result;
}

// A lone Union primitive is solid mass, not an open room - carving the exact
// same footprint back out with a Difference is what makes it one open,
// non-solid room, the same as every other test in this file's family.
std::vector<ArrangementPrimitive> openRoom(
    double minX, double minY, double maxX, double maxY,
    PrimitivePropertySet const& props) {
  auto area = (maxX - minX) * (maxY - minY);
  return {
      rectanglePrimitive(
          rectangle(minX, minY, maxX, maxY), Primitive::Operation::Union, 0, 1,
          props, area),
      rectanglePrimitive(
          rectangle(minX, minY, maxX, maxY), Primitive::Operation::Difference, 1,
          2, properties(props.floorZ, props.ceilingZ, 0.0f), area)};
}

// Locates the sole non-solid, non-exterior face, for tests whose arrangement
// has exactly one room of interest.
uint32_t soleRoomFace(bw::core::arr::ArrangementResult const& arrangement) {
  int found = -1;
  for (uint32_t i = 1; i < uint32_t(arrangement.faces.size()); ++i) {
    if (!arrangement.faces[i].solid) {
      require(found < 0, "expected exactly one non-solid room face");
      found = int(i);
    }
  }
  require(found >= 0, "no non-solid room face was found");
  return uint32_t(found);
}

void aUnionPrimitivesWaterLevelFillsTheRoomItCovers() {
  // The room's rawArea equals its faceArea (nothing else carves it further),
  // so its authored water level passes straight through as depth.
  auto arrangement = bw::core::arr::BuildArrangement(
      openRoom(0, 0, 100, 100, properties(0.0f, 48.0f, 20.0f)));
  auto depths = bw::core::arr::ComputeWaterLevels(*arrangement);

  auto faceIndex = soleRoomFace(*arrangement);
  requireNear(depths[faceIndex], 20.0,
              "a Union primitive's water level should pass through to the room it alone covers");
}

void anUnsetWaterLevelProducesZeroDepth() {
  auto arrangement = bw::core::arr::BuildArrangement(
      openRoom(0, 0, 100, 100, properties(0.0f, 48.0f, 0.0f)));
  auto depths = bw::core::arr::ComputeWaterLevels(*arrangement);

  auto faceIndex = soleRoomFace(*arrangement);
  requireNear(depths[faceIndex], 0.0,
              "a default, unset water level should produce zero depth");
}

// A base slab holds one Union primitive whose footprint is later split into
// two open rooms by a solid wall left standing between them (a strip of the
// base never carved by either Difference).
void aSinglePrimitiveSplitAcrossFacesDistributesProportionally() {
  auto base = rectanglePrimitive(
      rectangle(0, 0, 200, 100), Primitive::Operation::Union, 0, 1,
      properties(0.0f, 100.0f, 40.0f), 20000.0);
  auto roomA = rectanglePrimitive(
      rectangle(0, 0, 60, 100), Primitive::Operation::Difference, 1, 2,
      properties(0.0f, 100.0f, 0.0f), 6000.0);
  auto roomB = rectanglePrimitive(
      rectangle(80, 0, 200, 100), Primitive::Operation::Difference, 2, 3,
      properties(0.0f, 100.0f, 0.0f), 12000.0);

  auto arrangement = bw::core::arr::BuildArrangement({base, roomA, roomB});
  auto depths = bw::core::arr::ComputeWaterLevels(*arrangement);

  int roomAFace = -1, roomBFace = -1;
  for (uint32_t i = 1; i < uint32_t(arrangement->faces.size()); ++i) {
    auto const& face = arrangement->faces[i];
    if (face.solid) continue;
    if (face.primitiveIndex == 2) roomAFace = int(i);
    if (face.primitiveIndex == 3) roomBFace = int(i);
  }
  require(roomAFace >= 0 && roomBFace >= 0, "both carved rooms should be present as their own faces");

  // base's rawArea is 200*100 = 20000; roomA is 60*100 = 6000, roomB is
  // 120*100 = 12000, so each should get its proportional share of 40.
  requireNear(depths[roomAFace], 40.0 * 6000.0 / 20000.0,
              "the smaller room did not receive its proportional share of the split primitive's water level");
  requireNear(depths[roomBFace], 40.0 * 12000.0 / 20000.0,
              "the larger room did not receive its proportional share of the split primitive's water level");
}

// Only half of the base primitive's footprint is ever carved open; the other
// half remains an untouched, solid wall. The open room's depth must reflect
// base's full raw area as the denominator, not just the open half's area -
// so it gets half of the authored water level, not all of it.
void aPartlyCarvedPrimitiveContributesProportionallyLessVolume() {
  auto base = rectanglePrimitive(
      rectangle(0, 0, 100, 100), Primitive::Operation::Union, 0, 1,
      properties(0.0f, 48.0f, 40.0f), 10000.0);
  auto room = rectanglePrimitive(
      rectangle(0, 0, 50, 100), Primitive::Operation::Difference, 1, 2,
      properties(0.0f, 48.0f, 0.0f), 5000.0);

  auto arrangement = bw::core::arr::BuildArrangement({base, room});
  auto depths = bw::core::arr::ComputeWaterLevels(*arrangement);

  bool sawSolidRemainder = false;
  int roomFace = -1;
  for (uint32_t i = 1; i < uint32_t(arrangement->faces.size()); ++i) {
    auto const& face = arrangement->faces[i];
    if (face.solid) {
      sawSolidRemainder = true;
      continue;
    }
    if (face.primitiveIndex == 2) roomFace = int(i);
  }
  require(sawSolidRemainder, "the uncarved half of the base slab should remain solid");
  require(roomFace >= 0, "the carved half should be present as its own non-solid face");

  requireNear(depths[roomFace], 20.0,
              "an only-partly-carved Union primitive should contribute half its water level to the open half, "
              "using its full raw area as the denominator rather than only the open area");
}

void aWaterLevelOnANonUnionPrimitiveHasNoEffect() {
  auto base = rectanglePrimitive(
      rectangle(0, 0, 100, 100), Primitive::Operation::Union, 0, 1,
      properties(0.0f, 48.0f, 0.0f), 10000.0);
  auto room = rectanglePrimitive(
      rectangle(0, 0, 100, 100), Primitive::Operation::Difference, 1, 2,
      properties(0.0f, 48.0f, 30.0f), 10000.0);

  auto arrangement = bw::core::arr::BuildArrangement({base, room});
  auto depths = bw::core::arr::ComputeWaterLevels(*arrangement);

  auto faceIndex = soleRoomFace(*arrangement);
  requireNear(depths[faceIndex], 0.0,
              "a water level authored on a non-Union primitive must have no effect on any face's depth");
}

void aFacesDepthIsCappedAtItsOwnClearance() {
  auto arrangement = bw::core::arr::BuildArrangement(
      openRoom(0, 0, 100, 100, properties(10.0f, 34.0f, 1000.0f)));
  auto depths = bw::core::arr::ComputeWaterLevels(*arrangement);

  auto faceIndex = soleRoomFace(*arrangement);
  requireNear(depths[faceIndex], 24.0,
              "a face's depth should be capped at its own ceilingZ - floorZ clearance");
}

void getWaterDepthQueriesTheContainingFace() {
  ArrangementWorldData worldData(
      bw::core::arr::BuildArrangement(
          openRoom(0, 0, 100, 100, properties(0.0f, 48.0f, 18.0f))),
      wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}), 64.0f, 8.0f);

  requireNear(worldData.getWaterDepth({50.0f, 50.0f}), 18.0,
              "getWaterDepth should return the containing face's computed depth");
  requireNear(worldData.getWaterDepth({-200.0f, -200.0f}), 0.0,
              "getWaterDepth outside the arrangement should be zero");
}

}  // namespace

int main() {
  try {
    aUnionPrimitivesWaterLevelFillsTheRoomItCovers();
    anUnsetWaterLevelProducesZeroDepth();
    aSinglePrimitiveSplitAcrossFacesDistributesProportionally();
    aPartlyCarvedPrimitiveContributesProportionallyLessVolume();
    aWaterLevelOnANonUnionPrimitiveHasNoEffect();
    aFacesDepthIsCappedAtItsOwnClearance();
    getWaterDepthQueriesTheContainingFace();
    std::cout << "ComputeWaterLevels distributes authored water level across covered faces\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
