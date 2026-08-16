#pragma once

#include <functional>

#include <willpower/common/BoundingBox.h>

#include <willpower/collide/Simulation.h>

class WorldCollisionSim : public wp::collide::Simulation {
  std::set<uint32_t> getLineIndices(wp::BoundingBox const& bounds) const override;

public:
  explicit WorldCollisionSim(void* userObj = nullptr);

  void addSlidingCollider(
      wp::collide::Collider* collider,
      std::function<void()> const& onWallHit = {});

  std::vector<wp::collide::StaticLine> const& getLines() const;

  void clearLines();

  void addLine(wp::Vector2 const& v0, wp::Vector2 const& v1, bool doubleSided, uint32_t index);
};
