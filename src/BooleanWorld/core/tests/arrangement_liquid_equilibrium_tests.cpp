#include <cmath>
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
using bw::core::arr::ArrangementResult;
using bw::core::arr::Contour;
using bw::core::arr::ToFixedPointCoordinate;

constexpr double Epsilon = 0.001;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(double actual, double expected, std::string const& message) {
  if (std::abs(actual - expected) >= Epsilon) {
    throw std::runtime_error(
        message + " (expected " + std::to_string(expected) + ", got " +
        std::to_string(actual) + ")");
  }
}

Contour rectangle(double minX, double minY, double maxX, double maxY) {
  auto fp = [](double v) { return ToFixedPointCoordinate(v); };
  return {{fp(minX), fp(minY)}, {fp(maxX), fp(minY)},
          {fp(maxX), fp(maxY)}, {fp(minX), fp(maxY)}};
}

PrimitivePropertySet properties(float floorZ, float ceilingZ, float liquidLevel) {
  PrimitivePropertySet result;
  result.floorZ = floorZ;
  result.ceilingZ = ceilingZ;
  result.liquidLevel = liquidLevel;
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

// A slab of solid mass wide enough to leave a sealed rim around every room
// carved out of it below, so no room touches the Arrangement's outer boundary
// and drains.
ArrangementPrimitive slab(double minX, double minY, double maxX, double maxY) {
  return rectanglePrimitive(
      rectangle(minX, minY, maxX, maxY), Primitive::Operation::Union, 0, 1,
      properties(0.0f, 48.0f, 0.0f), (maxX - minX) * (maxY - minY));
}

// A Union primitive that adds no material of its own - it sits inside the
// slab already there - but pours the given liquid level into exactly the
// footprint one room will later be carved from. Its rawArea is that same
// footprint, so the room it covers receives the liquid level undiluted.
ArrangementPrimitive liquidSource(
    double minX, double minY, double maxX, double maxY, uint8_t priority,
    uint32_t primitiveIndex, float liquidLevel) {
  return rectanglePrimitive(
      rectangle(minX, minY, maxX, maxY), Primitive::Operation::Union, priority,
      primitiveIndex, properties(0.0f, 48.0f, liquidLevel),
      (maxX - minX) * (maxY - minY));
}

// One open room carved out of the slab, at its own floor and ceiling.
ArrangementPrimitive room(
    double minX, double minY, double maxX, double maxY, uint8_t priority,
    uint32_t primitiveIndex, float floorZ, float ceilingZ) {
  return rectanglePrimitive(
      rectangle(minX, minY, maxX, maxY), Primitive::Operation::Difference,
      priority, primitiveIndex, properties(floorZ, ceilingZ, 0.0f),
      (maxX - minX) * (maxY - minY));
}

// The non-solid face carved by the room primitive that was given this
// primitiveIndex.
uint32_t roomFace(ArrangementResult const& arrangement, uint32_t primitiveIndex) {
  for (uint32_t i = 1; i < uint32_t(arrangement.faces.size()); ++i) {
    auto const& face = arrangement.faces[i];
    if (!face.solid && face.primitiveIndex == primitiveIndex) {
      return i;
    }
  }
  throw std::runtime_error(
      "no non-solid face was carved by primitive " +
      std::to_string(primitiveIndex));
}

// Two rooms side by side inside a sealed slab, sharing the boundary at x=100.
// Liquid is poured only into the left room.
std::vector<ArrangementPrimitive> twoRooms(
    float liquidLevel, float ceilingA, float floorB, float ceilingB) {
  return {
      slab(-10, -10, 210, 110),
      liquidSource(0, 0, 100, 100, 1, 2, liquidLevel),
      room(0, 0, 100, 100, 2, 3, 0.0f, ceilingA),
      room(100, 0, 200, 100, 3, 4, floorB, ceilingB)};
}

void twoConnectedRoomsSettleAtOneSharedElevation() {
  // 20 units deep over the left room's 100x100 floor, spread across both
  // rooms' 20000 total area at a shared floor of zero.
  auto arrangement = bw::core::arr::BuildArrangement(
      twoRooms(20.0f, 48.0f, 0.0f, 48.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 3)], 10.0,
              "the room the liquid was poured into should have levelled off at the shared elevation");
  requireNear(depths[roomFace(*arrangement, 4)], 10.0,
              "a room with no authored liquid of its own should fill from its wetter neighbour");
}

// The same two rooms, but stacked so that the left room's ceiling is exactly
// the right room's floor: geometrically adjacent, with no headroom between
// them for liquid to cross.
void aZeroClearanceWallBlocksFlow() {
  auto arrangement = bw::core::arr::BuildArrangement(
      twoRooms(20.0f, 24.0f, 24.0f, 48.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 3)], 20.0,
              "liquid must stay put in its own room when the shared wall has no clearance");
  requireNear(depths[roomFace(*arrangement, 4)], 0.0,
              "a room sealed off by a zero-clearance wall must stay dry");
}

// Two pools at either end of a sealed slab, separated by a passage whose
// floor sits at elevation 10. Below that the pools are two separate bodies of
// liquid; above it they are one.
std::vector<ArrangementPrimitive> twoPoolsOverASaddle(
    float liquidLevelA, float liquidLevelB) {
  return {
      slab(-10, -10, 230, 110),
      liquidSource(0, 0, 100, 100, 1, 2, liquidLevelA),
      liquidSource(120, 0, 220, 100, 2, 3, liquidLevelB),
      room(0, 0, 100, 100, 3, 4, 0.0f, 48.0f),
      room(100, 0, 120, 100, 4, 5, 10.0f, 48.0f),
      room(120, 0, 220, 100, 5, 6, 0.0f, 48.0f)};
}

void poolsBelowTheSaddleStaySeparate() {
  auto arrangement =
      bw::core::arr::BuildArrangement(twoPoolsOverASaddle(6.0f, 2.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 4)], 6.0,
              "a pool that never rises to the passage floor should keep its own level");
  requireNear(depths[roomFace(*arrangement, 6)], 2.0,
              "the second pool should keep its own, different level while the two are unconnected");
  requireNear(depths[roomFace(*arrangement, 5)], 0.0,
              "the passage above both pools should be dry");
}

void poolsRisingPastTheSaddleMergeIntoOneLevel() {
  // 30 deep over 10000 plus 6 deep over 10000 is 360000 in total. The left
  // pool alone tops the passage floor at 10, so all three faces become one
  // body of liquid: 200000 fits below elevation 10, and the remaining 160000
  // rises over the combined 22000 area above it.
  auto arrangement =
      bw::core::arr::BuildArrangement(twoPoolsOverASaddle(30.0f, 6.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  auto expected = 10.0 + 160000.0 / 22000.0;
  requireNear(depths[roomFace(*arrangement, 4)], expected,
              "the fuller pool should have dropped to the merged equilibrium level");
  requireNear(depths[roomFace(*arrangement, 6)], expected,
              "the emptier pool should have risen to the same merged equilibrium level");
  requireNear(depths[roomFace(*arrangement, 5)], expected - 10.0,
              "the passage should hold the merged surface less its own higher floor");
}

// A high room at floor 20, a passage over its rim at floor 30, and a deep
// room at floor 0 beyond. Liquid poured into the high room can only reach the
// deep one by topping the passage floor at 30.
std::vector<ArrangementPrimitive> aRimAboveALowerRoom(
    float liquidLevelHigh, float liquidLevelLow) {
  return {
      slab(-10, -10, 230, 110),
      liquidSource(0, 0, 100, 100, 1, 2, liquidLevelHigh),
      liquidSource(120, 0, 220, 100, 2, 3, liquidLevelLow),
      room(0, 0, 100, 100, 3, 4, 20.0f, 60.0f),
      room(100, 0, 120, 100, 4, 5, 30.0f, 60.0f),
      room(120, 0, 220, 100, 5, 6, 0.0f, 60.0f)};
}

// Pouring over a saddle is a directed spill, not an equalization: only the
// liquid standing above the saddle crosses it, and the pouring stops the
// moment the donor's own surface falls back to the saddle. The two rooms
// therefore end at two different elevations, 30 and 10, with the passage
// between them exactly brim-full and dry.
void aPoolSpillingOverASaddleKeepsWhatStandsBelowIt() {
  auto arrangement =
      bw::core::arr::BuildArrangement(aRimAboveALowerRoom(20.0f, 0.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  auto high = depths[roomFace(*arrangement, 4)];
  auto passage = depths[roomFace(*arrangement, 5)];
  auto low = depths[roomFace(*arrangement, 6)];

  requireNear(high, 10.0,
              "the spilling room should keep the liquid standing below its rim at 30 rather than draining past it");
  requireNear(passage, 0.0,
              "the passage should be left exactly brim-full at its own floor");
  requireNear(low, 10.0,
              "the room below should hold only what actually poured over the rim");
  requireNear(high * 10000.0 + passage * 2000.0 + low * 10000.0, 200000.0,
              "the spill should move volume between rooms, not create or destroy it");
}

// The same three rooms, with the lower one already full enough that the liquid
// closes over the passage floor. Now they really are one body of liquid, and
// the directed spill gives way to a single shared surface.
void aSaddleSubmergedByBothPoolsStillMergesIntoOneSurface() {
  auto arrangement =
      bw::core::arr::BuildArrangement(aRimAboveALowerRoom(20.0f, 35.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  // 550000 in total: 400000 fits below the passage floor at 30, and the rest
  // rises over the combined 22000 area above it.
  auto surface = 30.0 + 150000.0 / 22000.0;
  requireNear(depths[roomFace(*arrangement, 4)], surface - 20.0,
              "the high room should share the merged surface once the saddle is submerged");
  requireNear(depths[roomFace(*arrangement, 5)], surface - 30.0,
              "the passage should share the merged surface once it is submerged");
  requireNear(depths[roomFace(*arrangement, 6)], surface,
              "the low room should share the merged surface once the saddle is submerged");
}

// Both rooms are sealed, and far more liquid is poured in than the two of them
// can hold between them.
void aSealedOverfullComponentCapsEveryFaceAtItsCeiling() {
  auto arrangement = bw::core::arr::BuildArrangement(
      twoRooms(1000.0f, 20.0f, 0.0f, 30.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 3)], 20.0,
              "an overfull component's lower-ceilinged room should fill to its own ceiling, not the shared surface");
  requireNear(depths[roomFace(*arrangement, 4)], 30.0,
              "an overfull component's higher-ceilinged room should fill to its own ceiling");
}

void aComponentTouchingTheExteriorDrainsThroughout() {
  // Sealed first, so the drained result below can only be the opening's
  // doing: the same two rooms hold the same liquid perfectly well when the
  // left one stops short of the slab's edge.
  auto sealed = bw::core::arr::BuildArrangement({
      slab(-10, -10, 210, 110),
      liquidSource(100, 0, 200, 100, 1, 2, 20.0f),
      room(0, 0, 100, 100, 2, 3, 0.0f, 48.0f),
      room(100, 0, 200, 100, 3, 4, 0.0f, 48.0f)});
  auto sealedDepths = bw::core::arr::ComputeLiquidLevels(*sealed);
  requireNear(sealedDepths[roomFace(*sealed, 3)], 10.0,
              "the sealed control case should hold its liquid");
  requireNear(sealedDepths[roomFace(*sealed, 4)], 10.0,
              "the sealed control case should hold its liquid");

  // The left room is now carved clean through the slab's left edge, so it
  // borders the Arrangement's unbounded exterior face. The liquid is still
  // authored in the right room, which only reaches the outside through it.
  auto arrangement = bw::core::arr::BuildArrangement({
      slab(-10, -10, 210, 110),
      liquidSource(100, 0, 200, 100, 1, 2, 20.0f),
      room(-20, 0, 100, 100, 2, 3, 0.0f, 48.0f),
      room(100, 0, 200, 100, 3, 4, 0.0f, 48.0f)});
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 4)], 0.0,
              "liquid in a component that reaches the outer boundary should drain away entirely");
  requireNear(depths[roomFace(*arrangement, 3)], 0.0,
              "the room that opens onto the outer boundary should be dry too");
}

void repeatedRunsProduceIdenticalResults() {
  auto primitives = twoPoolsOverASaddle(30.0f, 6.0f);
  auto first = bw::core::arr::ComputeLiquidLevels(
      *bw::core::arr::BuildArrangement(primitives));
  for (int run = 0; run < 4; ++run) {
    auto again = bw::core::arr::ComputeLiquidLevels(
        *bw::core::arr::BuildArrangement(primitives));
    require(again.size() == first.size(),
            "repeated runs produced a different number of faces");
    for (size_t i = 0; i < first.size(); ++i) {
      // Bit-for-bit, not near: the fill is exact, with no epsilon or
      // convergence threshold anywhere in it.
      require(again[i] == first[i],
              "repeated runs on the same input produced different liquid depths");
    }
  }
}

}  // namespace

int main() {
  try {
    twoConnectedRoomsSettleAtOneSharedElevation();
    aZeroClearanceWallBlocksFlow();
    poolsBelowTheSaddleStaySeparate();
    poolsRisingPastTheSaddleMergeIntoOneLevel();
    aPoolSpillingOverASaddleKeepsWhatStandsBelowIt();
    aSaddleSubmergedByBothPoolsStillMergesIntoOneSurface();
    aSealedOverfullComponentCapsEveryFaceAtItsCeiling();
    aComponentTouchingTheExteriorDrainsThroughout();
    repeatedRunsProduceIdenticalResults();
    std::cout << "Wet components settle into merging and spilling pools, capped and deterministic\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
