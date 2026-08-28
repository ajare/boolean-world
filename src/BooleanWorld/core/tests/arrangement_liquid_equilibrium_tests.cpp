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

// An ordinary solid room - just a Union primitive, the same as any room in an
// authored World - optionally carrying its own authored liquid level.
ArrangementPrimitive room(
    double minX, double minY, double maxX, double maxY, uint8_t priority,
    uint32_t primitiveIndex, float floorZ, float ceilingZ, float liquidLevel) {
  return rectanglePrimitive(
      rectangle(minX, minY, maxX, maxY), Primitive::Operation::Union,
      priority, primitiveIndex, properties(floorZ, ceilingZ, liquidLevel),
      (maxX - minX) * (maxY - minY));
}

// A thin solid collar pinned so its own floor equals its ceiling, at a
// room's own ceiling: zero clearance against that room (and any other room
// sharing the same ceiling), sealing it off from whatever lies beyond the
// collar - typically the Arrangement's own unbounded exterior face - without
// joining that room's own connected pool of liquid.
ArrangementPrimitive collar(
    double minX, double minY, double maxX, double maxY, float ceiling,
    uint8_t priority, uint32_t primitiveIndex) {
  return rectanglePrimitive(
      rectangle(minX, minY, maxX, maxY), Primitive::Operation::Union,
      priority, primitiveIndex, properties(ceiling, ceiling, 0.0f),
      (maxX - minX) * (maxY - minY));
}

// The solid face built by the room primitive that was given this
// primitiveIndex.
uint32_t roomFace(ArrangementResult const& arrangement, uint32_t primitiveIndex) {
  for (uint32_t i = 1; i < uint32_t(arrangement.faces.size()); ++i) {
    auto const& face = arrangement.faces[i];
    if (face.solid && face.primitiveIndex == primitiveIndex) {
      return i;
    }
  }
  throw std::runtime_error(
      "no solid face was found for primitive " + std::to_string(primitiveIndex));
}

// Two rooms side by side, sharing the boundary at x=100, each sealed off
// from the Arrangement's own unbounded exterior face by its own collar (the
// two rooms may have different ceilings, so each gets its own top, bottom,
// and outward-side collar rather than one shared ring). Liquid is poured
// only into the left room.
std::vector<ArrangementPrimitive> twoRooms(
    float liquidLevel, float ceilingA, float floorB, float ceilingB) {
  return {
      room(0, 0, 100, 100, 0, 1, 0.0f, ceilingA, liquidLevel),
      room(100, 0, 200, 100, 0, 2, floorB, ceilingB, 0.0f),
      collar(0, 100, 100, 110, ceilingA, 1, 101),
      collar(0, -10, 100, 0, ceilingA, 1, 102),
      collar(-10, -10, 0, 110, ceilingA, 1, 103),
      collar(100, 100, 200, 110, ceilingB, 1, 104),
      collar(100, -10, 200, 0, ceilingB, 1, 105),
      collar(200, -10, 210, 110, ceilingB, 1, 106),
  };
}

void twoConnectedRoomsSettleAtOneSharedElevation() {
  // 20 units deep over the left room's 100x100 floor, spread across both
  // rooms' 20000 total area at a shared floor of zero.
  auto arrangement = bw::core::arr::BuildArrangement(
      twoRooms(20.0f, 48.0f, 0.0f, 48.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 1)], 10.0,
              "the room the liquid was poured into should have levelled off at the shared elevation");
  requireNear(depths[roomFace(*arrangement, 2)], 10.0,
              "a room with no authored liquid of its own should fill from its wetter neighbour");
}

// The same two rooms, but stacked so that the left room's ceiling is exactly
// the right room's floor: geometrically adjacent, with no headroom between
// them for liquid to cross.
void aZeroClearanceWallBlocksFlow() {
  auto arrangement = bw::core::arr::BuildArrangement(
      twoRooms(20.0f, 24.0f, 24.0f, 48.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 1)], 20.0,
              "liquid must stay put in its own room when the shared wall has no clearance");
  requireNear(depths[roomFace(*arrangement, 2)], 0.0,
              "a room sealed off by a zero-clearance wall must stay dry");
}

// Two pools at either end of a sealed row, separated by a passage whose floor
// sits at elevation 10. Below that the pools are two separate bodies of
// liquid; above it they are one. All three rooms share one ceiling, so one
// collar strip seals the whole row.
std::vector<ArrangementPrimitive> twoPoolsOverASaddle(
    float liquidLevelA, float liquidLevelB) {
  return {
      room(0, 0, 100, 100, 0, 1, 0.0f, 48.0f, liquidLevelA),
      room(100, 0, 120, 100, 0, 2, 10.0f, 48.0f, 0.0f),
      room(120, 0, 220, 100, 0, 3, 0.0f, 48.0f, liquidLevelB),
      collar(0, 100, 220, 110, 48.0f, 1, 101),
      collar(0, -10, 220, 0, 48.0f, 1, 102),
      collar(-10, -10, 0, 110, 48.0f, 1, 103),
      collar(220, -10, 230, 110, 48.0f, 1, 104),
  };
}

void poolsBelowTheSaddleStaySeparate() {
  auto arrangement =
      bw::core::arr::BuildArrangement(twoPoolsOverASaddle(6.0f, 2.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 1)], 6.0,
              "a pool that never rises to the passage floor should keep its own level");
  requireNear(depths[roomFace(*arrangement, 3)], 2.0,
              "the second pool should keep its own, different level while the two are unconnected");
  requireNear(depths[roomFace(*arrangement, 2)], 0.0,
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
  requireNear(depths[roomFace(*arrangement, 1)], expected,
              "the fuller pool should have dropped to the merged equilibrium level");
  requireNear(depths[roomFace(*arrangement, 3)], expected,
              "the emptier pool should have risen to the same merged equilibrium level");
  requireNear(depths[roomFace(*arrangement, 2)], expected - 10.0,
              "the passage should hold the merged surface less its own higher floor");
}

// A high room at floor 20, a passage over its rim at floor 30, and a deep
// room at floor 0 beyond. Liquid poured into the high room can only reach the
// deep one by topping the passage floor at 30. All three rooms share one
// ceiling, so one collar strip seals the whole row.
std::vector<ArrangementPrimitive> aRimAboveALowerRoom(
    float liquidLevelHigh, float liquidLevelLow) {
  return {
      room(0, 0, 100, 100, 0, 1, 20.0f, 60.0f, liquidLevelHigh),
      room(100, 0, 120, 100, 0, 2, 30.0f, 60.0f, 0.0f),
      room(120, 0, 220, 100, 0, 3, 0.0f, 60.0f, liquidLevelLow),
      collar(0, 100, 220, 110, 60.0f, 1, 101),
      collar(0, -10, 220, 0, 60.0f, 1, 102),
      collar(-10, -10, 0, 110, 60.0f, 1, 103),
      collar(220, -10, 230, 110, 60.0f, 1, 104),
  };
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

  auto high = depths[roomFace(*arrangement, 1)];
  auto passage = depths[roomFace(*arrangement, 2)];
  auto low = depths[roomFace(*arrangement, 3)];

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
  requireNear(depths[roomFace(*arrangement, 1)], surface - 20.0,
              "the high room should share the merged surface once the saddle is submerged");
  requireNear(depths[roomFace(*arrangement, 2)], surface - 30.0,
              "the passage should share the merged surface once it is submerged");
  requireNear(depths[roomFace(*arrangement, 3)], surface,
              "the low room should share the merged surface once the saddle is submerged");
}

// Both rooms are sealed, and far more liquid is poured in than the two of them
// can hold between them.
void aSealedOverfullComponentCapsEveryFaceAtItsCeiling() {
  auto arrangement = bw::core::arr::BuildArrangement(
      twoRooms(1000.0f, 20.0f, 0.0f, 30.0f));
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 1)], 20.0,
              "an overfull component's lower-ceilinged room should fill to its own ceiling, not the shared surface");
  requireNear(depths[roomFace(*arrangement, 2)], 30.0,
              "an overfull component's higher-ceilinged room should fill to its own ceiling");
}

void aComponentTouchingTheExteriorDrainsThroughout() {
  // Sealed first, so the drained result below can only be the opening's
  // doing: the same two rooms hold the same liquid perfectly well when both
  // stay behind their collars.
  auto sealed = bw::core::arr::BuildArrangement({
      room(0, 0, 100, 100, 0, 1, 0.0f, 48.0f, 0.0f),
      room(100, 0, 200, 100, 0, 2, 0.0f, 48.0f, 20.0f),
      collar(0, 100, 200, 110, 48.0f, 1, 101),
      collar(0, -10, 200, 0, 48.0f, 1, 102),
      collar(-10, -10, 0, 110, 48.0f, 1, 103),
      collar(200, -10, 210, 110, 48.0f, 1, 104),
  });
  auto sealedDepths = bw::core::arr::ComputeLiquidLevels(*sealed);
  requireNear(sealedDepths[roomFace(*sealed, 1)], 10.0,
              "the sealed control case should hold its liquid");
  requireNear(sealedDepths[roomFace(*sealed, 2)], 10.0,
              "the sealed control case should hold its liquid");

  // The left room is left uncollared, but bordering the Arrangement's own
  // unbounded exterior face is not by itself an opening - those edges are
  // ordinary Border walls, solid by default like any authored room wall
  // (see ArrangementWorldData's authoredCollision and the liquid-adjacency
  // tests). Only its bottom edge is explicitly authored not to collide - a
  // genuine gap, the same as an open window - and that is what drains it.
  auto leftRoom = room(-20, 0, 100, 100, 0, 1, 0.0f, 48.0f, 0.0f);
  leftRoom.contourEdgeOverrides = {{false}};
  auto arrangement = bw::core::arr::BuildArrangement({
      leftRoom,
      room(100, 0, 200, 100, 0, 2, 0.0f, 48.0f, 20.0f),
      collar(100, 100, 200, 110, 48.0f, 1, 103),
      collar(100, -10, 200, 0, 48.0f, 1, 104),
      collar(200, -10, 210, 110, 48.0f, 1, 105),
  });
  auto depths = bw::core::arr::ComputeLiquidLevels(*arrangement);

  requireNear(depths[roomFace(*arrangement, 2)], 0.0,
              "liquid in a component that reaches the outer boundary should drain away entirely");
  requireNear(depths[roomFace(*arrangement, 1)], 0.0,
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
