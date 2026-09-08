#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/Elevation.h>
#include <core/PrimitivePropertySet.h>

#include <common/GameDefines.h>

#include "PlayerLiquidTraversal.h"

namespace {

using bw::core::Elevation;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;

constexpr int64_t U = 1000;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

Contour rectangle(int x0, int y0, int x1, int y1) {
  return {{x0 * U, y0 * U},
          {x1 * U, y0 * U},
          {x1 * U, y1 * U},
          {x0 * U, y1 * U}};
}

ArrangementPrimitive region(
    int x0, int x1, uint32_t primitiveIndex, Elevation floor,
    Elevation ceiling) {
  PrimitivePropertySet properties;
  properties.floorZ = floor;
  properties.ceilingZ = ceiling;
  ArrangementPrimitive result{
      {rectangle(x0, 0, x1, 30)},
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      primitiveIndex,
      primitiveIndex,
      properties};
  result.rawArea = double((x1 - x0) * 30);
  return result;
}

struct MantleWorld {
  std::shared_ptr<bw::core::ArrangementWorldData> data;
  uint32_t bankFace;
};

MantleWorld mantleWorld(Elevation bankFloor, Elevation bankCeiling) {
  auto arrangement = bw::core::arr::BuildArrangement(
      {region(-20, 0, 0, Elevation{-8.0f}, Elevation{40.0f}),
       region(0, 20, 1, bankFloor, bankCeiling)});
  auto data = std::make_shared<bw::core::ArrangementWorldData>(
      arrangement, wp::BoundingBox({-25.0f, -5.0f}, {50.0f, 40.0f}),
      8.0f);
  for (uint32_t face = 1; face < uint32_t(arrangement->faces.size()); ++face) {
    if (arrangement->faces[face].solid &&
        arrangement->faces[face].primitiveIndex == 1) {
      return {std::move(data), face};
    }
  }
  throw std::runtime_error("mantle fixture generated no bank face");
}

void climbOutRequiresLookingUp() {
  require(
      bw::app::isLookingUpForLiquidClimb(-0.01f),
      "an upward view was rejected for climbing out");
  require(
      !bw::app::isLookingUpForLiquidClimb(0.0f),
      "a level view was accepted for climbing out");
  require(
      !bw::app::isLookingUpForLiquidClimb(0.01f),
      "a downward view was accepted for climbing out");
}

void climbOutReachIsMeasuredFromThePlayersEye() {
  constexpr float playerFeetElevation = 10.0f;
  constexpr float eyeElevation =
      playerFeetElevation + BW_PLAYER_EYE_HEIGHT;

  require(
      bw::app::canClimbOutOfLiquidToFloor(
          playerFeetElevation, eyeElevation + BW_PLAYER_MANTLE_WATER),
      "a floor exactly one climb reach above the eye was rejected");
  require(
      bw::app::canClimbOutOfLiquidToFloor(
          playerFeetElevation, eyeElevation - BW_PLAYER_MANTLE_WATER),
      "a floor exactly one climb reach below the eye was rejected");
  require(
      !bw::app::canClimbOutOfLiquidToFloor(
          playerFeetElevation,
          eyeElevation + BW_PLAYER_MANTLE_WATER + 0.01f),
      "a floor beyond the climb reach above the eye was accepted");
  require(
      !bw::app::canClimbOutOfLiquidToFloor(
          playerFeetElevation,
          eyeElevation - BW_PLAYER_MANTLE_WATER - 0.01f),
      "a floor beyond the climb reach below the eye was accepted");
}

void climbOutRequiresFacingTheTargetPolygon() {
  require(
      bw::app::isFacingLiquidClimbTarget(0.01f),
      "a target polygon in front of the player was rejected");
  require(
      !bw::app::isFacingLiquidClimbTarget(0.0f),
      "a target polygon exactly beside the player was accepted");
  require(
      !bw::app::isFacingLiquidClimbTarget(-0.01f),
      "a target polygon behind the player was accepted");
}

void overlapSuppressionCannotCarryASwimmerAcrossTheWall() {
  require(
      !bw::app::maySuppressOverlappingTallStep(true),
      "overlap suppression let a swimmer bypass climb-out checks");
  require(
      bw::app::maySuppressOverlappingTallStep(false),
      "the swimmer restriction changed dry overlap traversal");
}

void downwardSwimmingDoesNotMasqueradeAsFallingFromALedge() {
  require(
      !bw::app::isDescendingForTallStepTraversal(true, -1.0f),
      "downward swim velocity removed the bank's collision wall");
  require(
      !bw::app::isDescendingForTallStepTraversal(true, 1.0f),
      "upward swim velocity was treated as a dry fall");
  require(
      bw::app::isDescendingForTallStepTraversal(false, -1.0f),
      "a dry fall no longer receives the tall-step traversal exemption");
  require(
      !bw::app::isDescendingForTallStepTraversal(false, 0.0f),
      "a stationary dry player was treated as descending");
}

void negativeWaterElevationDoesNotBypassClimbReach() {
  // A player floating with their base at -106 has an eye at -88. The floor at
  // zero is therefore 88 units above their eye, not within the 12-unit reach.
  require(
      !bw::app::canClimbOutOfLiquidToFloor(-106.0f, 0.0f),
      "negative player elevation bypassed the liquid climb reach");
}

void climbOutStillRequiresAnUpwardLift() {
  constexpr float playerFeetElevation = 10.0f;
  require(
      !bw::app::canClimbOutOfLiquidToFloor(
          playerFeetElevation, playerFeetElevation),
      "a floor at the swimmer's base was accepted as a climb");
  require(
      !bw::app::canClimbOutOfLiquidToFloor(
          playerFeetElevation, playerFeetElevation - 1.0f),
      "a floor below the swimmer was accepted as a climb");
}

void mantleLandingUsesTheActualProposedPosition() {
  wp::Vector2 const source{-6.1f, 15.0f};
  wp::Vector2 const landing{6.1f, 15.0f};
  constexpr float swimmerFeet = -8.0f;

  auto reachable = mantleWorld(
      Elevation{3.0f, {0.2f, 0.0f}}, Elevation{35.0f});
  require(
      bw::app::canLandLiquidMantle(
          *reachable.data, reachable.bankFace, source, landing, swimmerFeet),
      "a reachable walkable sloped bank rejected a valid mantle landing");

  // The plane is reachable at the edge but rises beyond arm's reach by the
  // time the whole player collider is actually over the bank.
  auto tooHigh = mantleWorld(
      Elevation{18.0f, {0.7f, 0.0f}}, Elevation{60.0f});
  require(
      !bw::app::canLandLiquidMantle(
          *tooHigh.data, tooHigh.bankFace, source, landing, swimmerFeet),
      "mantle reach was evaluated at the edge instead of the landing position");

  // This ceiling has room at the edge and less than player height where the
  // collider lands.
  auto lowCeiling = mantleWorld(
      Elevation{3.0f}, Elevation{29.0f, {-1.1f, 0.0f}});
  require(
      !bw::app::canLandLiquidMantle(
          *lowCeiling.data, lowCeiling.bankFace, source, landing,
          swimmerFeet),
      "mantle clearance was evaluated away from the landing position");

  auto tooSteep = mantleWorld(
      Elevation{-8.2f, {2.0f, 0.0f}}, Elevation{40.0f});
  require(
      !bw::app::canLandLiquidMantle(
          *tooSteep.data, tooSteep.bankFace, source, landing, swimmerFeet),
      "an unwalkably steep mantle landing was accepted");

  require(
      !bw::app::canLandLiquidMantle(
          *reachable.data, reachable.bankFace, source, {20.5f, 15.0f},
          swimmerFeet),
      "a mantle landing outside its target face passed final containment");
}

}  // namespace

int main() {
  try {
    climbOutRequiresLookingUp();
    climbOutReachIsMeasuredFromThePlayersEye();
    climbOutRequiresFacingTheTargetPolygon();
    overlapSuppressionCannotCarryASwimmerAcrossTheWall();
    downwardSwimmingDoesNotMasqueradeAsFallingFromALedge();
    negativeWaterElevationDoesNotBypassClimbReach();
    climbOutStillRequiresAnUpwardLift();
    mantleLandingUsesTheActualProposedPosition();
    std::cout << "Liquid climb-out validates the proposed landing position\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
