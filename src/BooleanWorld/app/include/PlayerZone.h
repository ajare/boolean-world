#pragma once

#include <algorithm>
#include <stdexcept>
#include <optional>
#include <vector>

#include <core/ArrangementWorldData.h>
#include <core/Defines.h>
#include "WorldCollisionSim.h"

namespace bw::app {

struct PlayerZoneCrossing {
  double fraction;
  core::ZoneId destination;
};

// Strict centre crossings only. A shared endpoint counts only when two
// collinear Border pieces continue across it; isolated endpoints and corners
// do not select a side. approach supports a sweep split exactly on a Border.
inline std::vector<PlayerZoneCrossing> queryPlayerZoneCrossings(
    core::ArrangementWorldData const& world,
    wp::Vector2 const& from, wp::Vector2 const& to,
    wp::Vector2 const* approach = nullptr) {
  std::vector<PlayerZoneCrossing> crossings;
  struct Endpoint {
    wp::Vector2 vertex, direction;
    double fraction, otherSide;
    core::ZoneId destination;
  };
  std::vector<Endpoint> endpoints;
  auto const& arrangement = world.getArrangement();
  auto cross = [](wp::Vector2 a, wp::Vector2 b) {
    return double(a.x) * b.y - double(a.y) * b.x;
  };
  for (auto const& wall : world.getWalls()) {
    if (!wall.sideZones) continue;
    auto const& edge = arrangement.edges[wall.edge];
    auto point = [&](uint32_t index) {
      auto const& p = arrangement.vertices[index];
      return wp::Vector2{core::arr::ToWorldCoordinate(p.x),
                         core::arr::ToWorldCoordinate(p.y)};
    };
    auto a = point(edge.v[0]);
    auto b = point(edge.v[1]);
    auto sideFrom = cross(b - a, from - a);
    auto sideTo = cross(b - a, to - a);
    bool continued = sideFrom == 0 && approach;
    if (continued) sideFrom = cross(b - a, *approach - a);
    if (!((sideFrom < 0 && sideTo > 0) ||
          (sideFrom > 0 && sideTo < 0))) continue;
    auto endA = cross(to - from, a - from);
    auto endB = cross(to - from, b - from);
    auto fraction = continued ? 0.0 : sideFrom / (sideFrom - sideTo);
    auto destination = (*wall.sideZones)[sideTo > 0 ? 0 : 1];
    if ((endA < 0 && endB > 0) || (endA > 0 && endB < 0)) {
      crossings.push_back({fraction, destination});
    } else if ((endA == 0) != (endB == 0)) {
      endpoints.push_back({endA == 0 ? a : b, b - a, fraction,
                           endA == 0 ? endB : endA, destination});
    }
  }
  for (size_t i = 0; i < endpoints.size(); ++i) {
    auto const& a = endpoints[i];
    for (size_t j = i + 1; j < endpoints.size(); ++j) {
      auto const& b = endpoints[j];
      if (a.vertex.x == b.vertex.x && a.vertex.y == b.vertex.y &&
          cross(a.direction, b.direction) == 0 &&
          a.otherSide * b.otherSide < 0 && a.destination == b.destination) {
        crossings.push_back({a.fraction, a.destination});
      }
    }
  }
  std::sort(crossings.begin(), crossings.end(),
      [](auto const& a, auto const& b) {
        if (a.fraction != b.fraction) return a.fraction < b.fraction;
        return a.destination < b.destination;
      });
  // Generated Borders have one relationship at each location. Resolve any
  // coincident candidates once, independent of wall iteration order.
  crossings.erase(std::unique(crossings.begin(), crossings.end(),
      [](auto const& a, auto const& b) { return a.fraction == b.fraction; }), crossings.end());
  return crossings;
}

// Boolean World runtime state, deliberately independent of physical stats,
// authored data and generated snapshot lifetime.
class PlayerZone {
 public:
  core::ZoneId current() const { return mCurrent; }
  void set(core::ZoneId zone) {
    if (!core::isKnownZone(zone)) throw std::invalid_argument("Unknown player Zone");
    mCurrent = zone;
    mApproach.reset();
  }
  // Map load, map entry and respawn initialize; relocation never calls this.
  void initialize() { set(core::ZoneId::Euclidean); }

  void applyResolvedMovement(core::ArrangementWorldData const& world,
      std::vector<WorldCollisionSim::PlayerMovementSegment> const& trace) {
    for (auto const& segment : trace) {
      if (segment.type != WorldCollisionSim::PlayerMovementSegmentType::Swept) {
        mApproach.reset();
        continue;
      }
      if (segment.from.x == segment.to.x && segment.from.y == segment.to.y) continue;
      auto approach = mApproach && mApproach->to.x == segment.from.x &&
          mApproach->to.y == segment.from.y ? &mApproach->from : nullptr;
      for (auto const& crossing : queryPlayerZoneCrossings(world, segment.from, segment.to, approach))
        mCurrent = crossing.destination;
      mApproach = segment;
    }
  }

  void resolveMovement(WorldCollisionSim& simulation, float frameTime) const {
    switch (mCurrent) {
      case core::ZoneId::Euclidean: resolveEuclidean(simulation, frameTime); return;
      case core::ZoneId::NegativeSpace: resolveNegativeSpace(simulation, frameTime); return;
    }
    throw std::invalid_argument("Unknown player collision Zone");
  }

 private:
  // Both strategies retain ordinary two-sided generated-wall collision. The
  // Negative Space strategy additionally owns the invisible engine boundary;
  // it is not authored geometry and cannot participate in Zone crossings.
  static void resolveEuclidean(WorldCollisionSim& simulation, float dt) { simulation.update(dt); }
  static void resolveNegativeSpace(WorldCollisionSim& simulation, float dt) {
    constexpr float halfExtent = float(BW_WORLD_SIZE) * 0.5f;
    simulation.updateWithinBoundary(
        dt, {{-halfExtent, -halfExtent},
             {float(BW_WORLD_SIZE), float(BW_WORLD_SIZE)}});
  }
  core::ZoneId mCurrent{core::ZoneId::Euclidean};
  std::optional<WorldCollisionSim::PlayerMovementSegment> mApproach;
};

} // namespace bw::app
