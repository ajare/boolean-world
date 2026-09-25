#pragma once

#include <algorithm>
#include <stdexcept>
#include <optional>
#include <vector>

#include <core/ArrangementWorldData.h>
#include <core/Defines.h>
#include <core/Phantom.h>
#include <common/GameDefines.h>
#include "WorldCollisionSim.h"

namespace bw::app {

struct PlayerZoneCrossing {
  double fraction;
  core::ZoneId destination;
  uint32_t wallIndex{~0u};
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
    uint32_t wallIndex;
  };
  std::vector<Endpoint> endpoints;
  auto const& arrangement = world.getArrangement();
  auto cross = [](wp::Vector2 a, wp::Vector2 b) {
    return double(a.x) * b.y - double(a.y) * b.x;
  };
  for (uint32_t wallIndex = 0; wallIndex < world.getWalls().size(); ++wallIndex) {
    auto const& wall = world.getWalls()[wallIndex];
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
      crossings.push_back({fraction, destination, wallIndex});
    } else if ((endA == 0) != (endB == 0)) {
      endpoints.push_back({endA == 0 ? a : b, b - a, fraction,
                           endA == 0 ? endB : endA, destination, wallIndex});
    }
  }
  for (size_t i = 0; i < endpoints.size(); ++i) {
    auto const& a = endpoints[i];
    for (size_t j = i + 1; j < endpoints.size(); ++j) {
      auto const& b = endpoints[j];
      if (a.vertex.x == b.vertex.x && a.vertex.y == b.vertex.y &&
          cross(a.direction, b.direction) == 0 &&
          a.otherSide * b.otherSide < 0 && a.destination == b.destination) {
        crossings.push_back({a.fraction, a.destination, a.wallIndex});
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
      std::vector<WorldCollisionSim::PlayerMovementSegment> const& trace,
      float feet = 0.0f, float radius = BW_PLAYER_RADIUS, float height = BW_PLAYER_HEIGHT) {
    for (auto const& segment : trace) {
      if (segment.type != WorldCollisionSim::PlayerMovementSegmentType::Swept) {
        mApproach.reset();
        continue;
      }
      if (segment.from.x == segment.to.x && segment.from.y == segment.to.y) continue;
      auto approach = mApproach && mApproach->to.x == segment.from.x &&
          mApproach->to.y == segment.from.y ? &mApproach->from : nullptr;
      for (auto const& crossing : queryPlayerZoneCrossings(world, segment.from, segment.to, approach)) {
        if (!acceptsCrossing(world, crossing, segment.from, segment.to, feet, radius, height)) continue;
        mCurrent = crossing.destination;
      }
      mApproach = segment;
    }
  }

  // The game uses these events during collision resolution, not after it.
  // Clearance is checked before committing, so the physics engine cannot
  // reject the event's position after its Zone has already changed.
  std::optional<PlayerZoneCrossing> nextCrossing(core::ArrangementWorldData const& world,
      wp::Vector2 const& from, wp::Vector2 const& to, float feet,
      float radius = BW_PLAYER_RADIUS, float height = BW_PLAYER_HEIGHT) const {
    auto approach = mApproach && mApproach->to == from ? &mApproach->from : nullptr;
    for (auto const& crossing : queryPlayerZoneCrossings(world, from, to, approach)) {
      if (crossing.destination == mCurrent) continue;
      if (acceptsCrossing(world, crossing, from, to, feet, radius, height)) return crossing;
    }
    return std::nullopt;
  }

  void configureMovement(WorldCollisionSim& simulation, core::ArrangementWorldData const& world,
      float const& feet, std::function<void(core::ZoneId, wp::Vector2 const&)> changed = {}) {
    struct QueryCache {
      wp::Vector2 from, to;
      float feet{};
      core::ZoneId zone{};
      bool valid{false};
      std::optional<PlayerZoneCrossing> crossing;
    };
    auto cache = std::make_shared<QueryCache>();
    // Every candidate Zone line asks about the same sweep. Evaluate its ordered
    // crossings once, not once per line (which would make movement quadratic).
    auto query = [this, &world, &feet, cache](auto const& from, auto const& to) {
      if (!cache->valid || cache->from != from || cache->to != to ||
          cache->feet != feet || cache->zone != mCurrent) {
        *cache = {from, to, feet, mCurrent, true, nextCrossing(world, from, to, feet)};
      }
      return cache->crossing;
    };
    simulation.setZoneCallbacks(
        [query](auto const& from, auto const& to, uint32_t wall) -> std::optional<double> {
          auto crossing = query(from, to);
          if (!crossing || crossing->wallIndex != wall) return std::nullopt;
          return crossing->fraction;
        },
        [this, query, changed](auto const& from, auto const& to, uint32_t wall) {
          auto crossing = query(from, to);
          if (!crossing || crossing->wallIndex != wall) throw std::logic_error("Zone crossing changed during sweep");
          set(crossing->destination);
          if (changed) changed(mCurrent, from + (to - from) * float(crossing->fraction));
        },
        [this] { return mCurrent == core::ZoneId::Phantom; },
        [this] { return mCurrent != core::ZoneId::Euclidean; });
  }

  void rememberMovement(std::vector<WorldCollisionSim::PlayerMovementSegment> const& trace) {
    for (auto const& segment : trace) {
      if (segment.type == WorldCollisionSim::PlayerMovementSegmentType::PortalRelocation) mApproach.reset();
      else if (segment.from != segment.to) mApproach = segment;
    }
  }

  void resolveMovement(WorldCollisionSim& simulation, float frameTime) const {
    if (simulation.hasZoneCallbacks()) {
      constexpr float half = float(BW_WORLD_SIZE) * 0.5f;
      // Boundary and ordinary lines are enabled dynamically by the current
      // strategy, so a single frame can enter and leave Phantom safely.
      simulation.updateWithinBoundary(frameTime,
          {{-half, -half}, {float(BW_WORLD_SIZE), float(BW_WORLD_SIZE)}});
      return;
    }
    switch (mCurrent) {
      case core::ZoneId::Euclidean: resolveEuclidean(simulation, frameTime); return;
      case core::ZoneId::NegativeSpace: resolveNegativeSpace(simulation, frameTime); return;
      case core::ZoneId::Phantom: {
        constexpr float half = float(BW_WORLD_SIZE) * 0.5f;
        simulation.updatePhantom(frameTime, {{-half, -half}, {float(BW_WORLD_SIZE), float(BW_WORLD_SIZE)}});
        return;
      }
    }
    throw std::invalid_argument("Unknown player collision Zone");
  }

 private:
  bool acceptsCrossing(core::ArrangementWorldData const& world,
      PlayerZoneCrossing const& crossing, wp::Vector2 const& from,
      wp::Vector2 const& to, float feet, float radius, float height) const {
    if (mCurrent != core::ZoneId::Phantom && crossing.destination != core::ZoneId::Phantom) return true;
    auto const& wall = world.getWalls()[crossing.wallIndex];
    if (!core::isPhantomAperture(wall) ||
        (mCurrent == core::ZoneId::Phantom && crossing.destination != core::ZoneId::Euclidean)) return false;
    // Entry retains the ordinary Euclidean Border-crossing rule. Phantom's
    // frozen-height aperture/placement check applies only to a return.
    if (mCurrent != core::ZoneId::Phantom) return true;
    auto frame = core::arr::OrientArrangementWall(world.getArrangement(), wall);
    auto point = from + (to - from) * float(crossing.fraction);
    auto span = frame.v1 - frame.v0;
    auto along = std::clamp((point - frame.v0).dot(span) / span.lengthSq(), 0.0f, 1.0f);
    if (feet < std::lerp(frame.bottomZ[0], frame.bottomZ[1], along) - 0.001f ||
        feet + height > std::lerp(frame.topZ[0], frame.topZ[1], along) + 0.001f) return false;
    if (crossing.destination == core::ZoneId::Euclidean) {
      auto inside = point + frame.normal * 0.001f;
      auto surface = world.getSurfaceSample(inside);
      if (!surface || feet < surface->floorElevation - 0.001f ||
          feet + height > surface->ceilingElevation + 0.001f) return false;
      for (auto index : world.getWallsNearForTraversal(inside, radius + 0.01f, inside, false))
        for (auto const& segment : world.getWallCollisionSegments(index))
          if (point.distanceToLine(segment.v0, segment.v1) < radius - 0.001f) return false;
    }
    return true;
  }

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
