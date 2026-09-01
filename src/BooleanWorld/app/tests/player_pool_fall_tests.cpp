// Falling from a dry bank into a deep, water-filled pit - the shape of the
// deep flooded rooms in world-mines-2 (floorZ -128, liquid level 44). The
// player walks off the bank, falls, and must end up swimming in the pool
// rather than pinned against the bank they fell from.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <willpower/collide/ColliderCircle.h>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/LiquidProperties.h>
#include <core/PrimitivePropertySet.h>

#include "common/GameDefines.h"

#include "PlayerLiquidTraversal.h"
#include "PlayerVerticalPhysics.h"
#include "PlayerWallDepenetration.h"
#include "WorldCollisionSim.h"

namespace {
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

Contour rectContour(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

PrimitivePropertySet pitProperties(
    float floorZ, float ceilingZ, float liquidLevel) {
  PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  properties.liquidLevel = liquidLevel;
  return properties;
}

std::shared_ptr<bw::core::ArrangementWorldData> makePitWorld(
    float pitFloorZ, float liquidLevel) {
  ArrangementPrimitive ground{
      {rectContour(-200 * U, -200 * U, 200 * U, 200 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      0,
      1,
      pitProperties(0.0f, 40.0f, 0.0f)};
  ground.rawArea = 400.0 * 400.0;

  ArrangementPrimitive pit{
      {rectContour(-50 * U, -50 * U, 50 * U, 50 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      1,
      2,
      pitProperties(pitFloorZ, 40.0f, liquidLevel)};
  pit.rawArea = 100.0 * 100.0;

  auto arrangement = bw::core::arr::BuildArrangement({ground, pit});
  wp::BoundingBox extents({-250.0f, -250.0f}, {500.0f, 500.0f});
  return std::make_shared<bw::core::ArrangementWorldData>(
      arrangement, extents, 64.0f);
}

struct Frame {
  float time;
  wp::Vector2 position;
  float floorZ;
  float verticalVelocity;
  bool swimming;
  uint32_t wallLines;
};

// One frame of StatePlayBooleanWorld's player update, in its real order:
// horizontal walls are chosen against last frame's settled state, the collider
// resolves the horizontal move, then the vertical step runs.
class PlayerHarness {
  bw::core::ArrangementWorldData const& mData;
  WorldCollisionSim mSimulation;
  wp::collide::ColliderCircle* mCollider{nullptr};
  bw::app::PlayerVerticalState mState{0.0f, 0.0f};
  wp::Vector2 mPosition;
  float mTime{0.0f};

public:
  static constexpr float FrameTime = 1.0f / 60.0f;

  PlayerHarness(
      bw::core::ArrangementWorldData const& data, wp::Vector2 startPosition)
      : mData(data), mPosition(startPosition) {
    auto colliderOwner = std::make_unique<wp::collide::ColliderCircle>(
        startPosition, float(BW_PLAYER_RADIUS));
    mCollider = colliderOwner.get();
    mSimulation.addSlidingCollider(std::move(colliderOwner));
  }

  wp::Vector2 const& position() const { return mPosition; }

  float floorZ() const { return mState.floorZ; }

  bool swimming() const {
    auto liquidSurface = mData.getLiquidSurfaceHeight(mPosition);
    auto submersion =
        std::isfinite(liquidSurface)
            ? std::clamp(liquidSurface - mState.floorZ, 0.0f,
                         float(BW_PLAYER_HEIGHT))
            : 0.0f;
    return submersion >= BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION *
                             float(BW_PLAYER_HEIGHT);
  }

  Frame step(wp::Vector2 const& walkDirection) {
    auto const& arrangement = mData.getArrangement();
    auto const& walls = mData.getWalls();
    auto isSwimming = swimming();

    auto velocity = walkDirection * float(BW_PLAYER_SPEED);
    auto predicted = mPosition + velocity * FrameTime;

    // createWorldCollisions
    mSimulation.clearLines();
    auto descending = bw::app::isDescendingForTallStepTraversal(
        isSwimming, mState.verticalVelocity);
    uint32_t lineCount = 0;
    std::vector<bw::app::WallSegment> addedWalls;
    for (auto wallIndex : mData.getWallsNearForTraversal(
             predicted, float(BW_PLAYER_SPEED) + float(BW_PLAYER_RADIUS),
             mPosition, descending)) {
      auto const& wall = walls[wallIndex];
      auto const& edge = arrangement.edges[wall.edge];
      auto const& fixed0 = arrangement.vertices[edge.v[0]];
      auto const& fixed1 = arrangement.vertices[edge.v[1]];
      wp::Vector2 v0{bw::core::arr::ToWorldCoordinate(fixed0.x),
                     bw::core::arr::ToWorldCoordinate(fixed0.y)};
      wp::Vector2 v1{bw::core::arr::ToWorldCoordinate(fixed1.x),
                     bw::core::arr::ToWorldCoordinate(fixed1.y)};

      auto blocksOnlyByStepHeight =
          wall.kind == bw::core::arr::ArrangementWallKind::FloorStep &&
          !edge.collidesOverride.value_or(false) &&
          wall.clearance >= BW_PLAYER_HEIGHT;
      if (blocksOnlyByStepHeight &&
          bw::app::maySuppressOverlappingTallStep(isSwimming) &&
          mPosition.distanceToLine(v0, v1) < BW_PLAYER_RADIUS) {
        continue;
      }

      mSimulation.addLine(v0, v1, wallIndex);
      addedWalls.push_back({v0, v1});
      ++lineCount;
    }

    // liftPlayerOffOverlappingWalls
    auto lifted = bw::app::resolveWallOverlap(
        mPosition, float(BW_PLAYER_RADIUS), addedWalls);
    if (lifted != mPosition &&
        mData.getContainingFaceIndex(lifted) ==
            mData.getContainingFaceIndex(mPosition)) {
      mPosition = lifted;
      mCollider->_setPosition(lifted);
    }

    mCollider->setMovement(velocity);
    mSimulation.update(FrameTime);
    mPosition = mCollider->getCentre();

    // updatePlayerVerticalPhysics
    bw::app::PlayerVerticalInputs inputs;
    inputs.inWorld = mData.getContainingFaceIndex(mPosition) != ~0u;
    inputs.frameTime = FrameTime;
    inputs.swimEffort = 0.0f;
    if (inputs.inWorld) {
      inputs.targetFloor = mData.getFloorHeight(mPosition);
      inputs.liquidSurface = mData.getLiquidSurfaceHeight(mPosition);
      inputs.liquid =
          bw::core::GetLiquidProperties(mData.getLiquidType(mPosition));
    }
    mState = bw::app::stepPlayerVerticalPhysics(mState, inputs);

    mTime += FrameTime;
    return {mTime,      mPosition, mState.floorZ, mState.verticalVelocity,
            isSwimming, lineCount};
  }

  std::vector<Frame> run(wp::Vector2 const& walkDirection, float seconds) {
    std::vector<Frame> trace;
    auto frames = uint32_t(seconds / FrameTime);
    for (uint32_t i = 0; i < frames; ++i) {
      trace.push_back(step(walkDirection));
    }
    return trace;
  }

  // Walk forwards until the fall begins, then release the stick and settle -
  // someone who stepped off the edge without meaning to swim anywhere.
  std::vector<Frame> stepOffTheEdgeAndSettle(
      wp::Vector2 const& walkDirection, float settleSeconds) {
    std::vector<Frame> trace;
    for (uint32_t i = 0; i < 600 && mState.floorZ > -1.0f; ++i) {
      trace.push_back(step(walkDirection));
    }
    auto settled = run(wp::Vector2::ZERO, settleSeconds);
    trace.insert(trace.end(), settled.begin(), settled.end());
    return trace;
  }
};

// The mines-2 shape: a 100x100 pit 128 below the bank, holding 44 of water
// (surface at -84). Walking straight off the bank must drop the player into
// the pool and leave them swimming there, not stopped at the bank edge.
void walkingOffABankIntoADeepPoolDoesNotPinThePlayerToTheBank() {
  auto data = makePitWorld(-128.0f, 44.0f);
  PlayerHarness player(*data, {0.0f, 120.0f});
  player.run({0.0f, -1.0f}, 6.0f);

  require(player.floorZ() < -50.0f,
          "player never fell into the pool (floorZ " +
              std::to_string(player.floorZ()) + ")");
  require(player.position().y < 44.0f - 1.0f,
          "player was pinned at the bank edge (y " +
              std::to_string(player.position().y) + ")");
}

// The player lets go of the stick the moment they step off - they meant to
// stop at the edge, not to swim across - so they land in the water a couple of
// units from the bank they fell from, inside the tall floor step that is
// reinstated as soon as they are submerged enough to count as swimming. A
// collider that begins a frame intersecting a line cannot move in any
// direction at all, so without resolveWallOverlap this leaves them frozen
// against the bank, unable even to swim away from it.
void aSwimmerWhoFellInBesideTheBankCanStillSwimAway() {
  auto data = makePitWorld(-128.0f, 44.0f);

  struct Direction {
    char const* name;
    wp::Vector2 heading;
  };
  // Away from the bank, and along it in both directions. Every one of these is
  // open water: none of them crosses the bank the player fell from.
  Direction const directions[] = {{"away from the bank", {0.0f, -1.0f}},
                                  {"left along the bank", {-1.0f, 0.0f}},
                                  {"right along the bank", {1.0f, 0.0f}}};

  for (auto const& direction : directions) {
    PlayerHarness swimmer(*data, {0.0f, 120.0f});
    swimmer.stepOffTheEdgeAndSettle({0.0f, -1.0f}, 3.0f);
    require(swimmer.swimming(), "player did not end up swimming in the pool");
    require(50.0f - swimmer.position().y < 2.0f * float(BW_PLAYER_RADIUS),
            "fixture no longer lands the player against the bank (y " +
                std::to_string(swimmer.position().y) + ")");

    auto before = swimmer.position();
    swimmer.run(direction.heading, 1.0f);
    auto travelled = (swimmer.position() - before).length();
    require(travelled > 10.0f,
            std::string("swimmer stuck against the bank they fell from: ") +
                direction.name + " moved only " + std::to_string(travelled));
  }
}

// Lifting the swimmer clear of the bank must not open it. A swimmer leaves
// liquid only through tryClimbOutOfLiquid, which checks pitch, facing and
// reach first - swimming straight at the bank must still get them nowhere.
void liftingASwimmerClearOfTheBankDoesNotLetThemSwimOverIt() {
  auto data = makePitWorld(-128.0f, 44.0f);
  PlayerHarness swimmer(*data, {0.0f, 120.0f});
  swimmer.stepOffTheEdgeAndSettle({0.0f, -1.0f}, 3.0f);
  require(swimmer.swimming(), "player did not end up swimming in the pool");

  swimmer.run({0.0f, 1.0f}, 2.0f);
  require(swimmer.position().y < 50.0f,
          "a swimmer crossed the bank horizontally (y " +
              std::to_string(swimmer.position().y) + ")");
  require(swimmer.floorZ() < -50.0f,
          "a swimmer was lifted out of the pool by pushing at its bank (floorZ " +
              std::to_string(swimmer.floorZ()) + ")");
}

// Control: the same pit with no water in it. A dry faller lands on the bottom
// just as close to the bank, and the overlapping-tall-step suppression in
// createWorldCollisions keeps them free - so it is the swimming state, not the
// closeness itself, that decided whether the player could move.
void aDryFallerLandingBesideTheSameBankIsNotStuck() {
  auto data = makePitWorld(-128.0f, 0.0f);
  PlayerHarness faller(*data, {0.0f, 120.0f});
  faller.stepOffTheEdgeAndSettle({0.0f, -1.0f}, 3.0f);
  require(!faller.swimming(), "dry control fixture is somehow swimming");

  auto before = faller.position();
  faller.run({0.0f, -1.0f}, 1.0f);
  auto travelled = (faller.position() - before).length();
  require(travelled > 10.0f,
          "dry faller was stuck against the bank too (moved " +
              std::to_string(travelled) + ")");
}
}  // namespace

int main() {
  try {
    walkingOffABankIntoADeepPoolDoesNotPinThePlayerToTheBank();
    aSwimmerWhoFellInBesideTheBankCanStillSwimAway();
    liftingASwimmerClearOfTheBankDoesNotLetThemSwimOverIt();
    aDryFallerLandingBesideTheSameBankIsNotStuck();
  } catch (std::exception const& e) {
    std::cerr << "FAILED: " << e.what() << "\n";
    return 1;
  }
  std::cout << "All player pool fall tests passed\n";
  return 0;
}
