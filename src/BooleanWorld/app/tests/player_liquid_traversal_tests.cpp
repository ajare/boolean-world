#include <iostream>
#include <stdexcept>

#include <common/GameDefines.h>

#include "PlayerLiquidTraversal.h"

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
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
  constexpr float playerFloorZ = 10.0f;
  constexpr float eyeZ = playerFloorZ + BW_PLAYER_EYE_HEIGHT;

  require(
      bw::app::canClimbOutOfLiquidToFloor(
          playerFloorZ, eyeZ + BW_PLAYER_MAX_CLIMB_OUT_HEIGHT),
      "a floor exactly one climb reach above the eye was rejected");
  require(
      bw::app::canClimbOutOfLiquidToFloor(
          playerFloorZ, eyeZ - BW_PLAYER_MAX_CLIMB_OUT_HEIGHT),
      "a floor exactly one climb reach below the eye was rejected");
  require(
      !bw::app::canClimbOutOfLiquidToFloor(
          playerFloorZ, eyeZ + BW_PLAYER_MAX_CLIMB_OUT_HEIGHT + 0.01f),
      "a floor beyond the climb reach above the eye was accepted");
  require(
      !bw::app::canClimbOutOfLiquidToFloor(
          playerFloorZ, eyeZ - BW_PLAYER_MAX_CLIMB_OUT_HEIGHT - 0.01f),
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

void climbOutStillRequiresAnUpwardLift() {
  constexpr float playerFloorZ = 10.0f;
  require(
      !bw::app::canClimbOutOfLiquidToFloor(playerFloorZ, playerFloorZ),
      "a floor at the swimmer's base was accepted as a climb");
  require(
      !bw::app::canClimbOutOfLiquidToFloor(playerFloorZ, playerFloorZ - 1.0f),
      "a floor below the swimmer was accepted as a climb");
}

}  // namespace

int main() {
  try {
    climbOutRequiresLookingUp();
    climbOutReachIsMeasuredFromThePlayersEye();
    climbOutRequiresFacingTheTargetPolygon();
    overlapSuppressionCannotCarryASwimmerAcrossTheWall();
    downwardSwimmingDoesNotMasqueradeAsFallingFromALedge();
    climbOutStillRequiresAnUpwardLift();
    std::cout << "Liquid climb-out reach is measured from player eye level\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
