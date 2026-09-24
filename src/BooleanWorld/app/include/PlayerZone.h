#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

#include <core/ArrangementWorldData.h>
#include "WorldCollisionSim.h"

namespace bw::app {

struct PlayerZoneCrossing {
  double fraction;
  core::ZoneId destination;
};

// Strict centre crossings only: contact at either travel endpoint, grazing,
// and contact with a Border endpoint do not select a side.
inline std::vector<PlayerZoneCrossing> queryPlayerZoneCrossings(
    core::ArrangementWorldData const& world,
    wp::Vector2 const& from, wp::Vector2 const& to) {
  std::vector<PlayerZoneCrossing> crossings;
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
    if (!((sideFrom < 0 && sideTo > 0) ||
          (sideFrom > 0 && sideTo < 0))) continue;
    auto endA = cross(to - from, a - from);
    auto endB = cross(to - from, b - from);
    if (!((endA < 0 && endB > 0) || (endA > 0 && endB < 0))) continue;
    crossings.push_back({sideFrom / (sideFrom - sideTo),
                         (*wall.sideZones)[sideTo > 0 ? 0 : 1]});
  }
  std::stable_sort(crossings.begin(), crossings.end(),
      [](auto const& a, auto const& b) { return a.fraction < b.fraction; });
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
  }
  // Map load, map entry and respawn initialize; relocation never calls this.
  void initialize() { mCurrent = core::ZoneId::Euclidean; }

  void applyResolvedMovement(core::ArrangementWorldData const& world,
      std::vector<WorldCollisionSim::PlayerMovementSegment> const& trace) {
    for (auto const& segment : trace) {
      if (segment.type != WorldCollisionSim::PlayerMovementSegmentType::Swept) continue;
      for (auto const& crossing : queryPlayerZoneCrossings(world, segment.from, segment.to))
        set(crossing.destination);
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
  // Independently dispatched strategies currently share ordinary two-sided
  // blocking-wall collision. Zone is not a collision/visibility override.
  static void resolveEuclidean(WorldCollisionSim& simulation, float dt) { simulation.update(dt); }
  static void resolveNegativeSpace(WorldCollisionSim& simulation, float dt) { simulation.update(dt); }
  core::ZoneId mCurrent{core::ZoneId::Euclidean};
};

} // namespace bw::app
