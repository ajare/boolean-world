#pragma once

#include <functional>
#include <memory>

#include <willpower/common/BoundingBox.h>

#include <willpower/collide/Simulation.h>

class WorldCollisionSim : public wp::collide::Simulation {
  void getLineIndices(
      wp::BoundingBox const& bounds,
      std::vector<uint32_t>& indices) const override;

public:
  explicit WorldCollisionSim(void* userObj = nullptr);

  void addSlidingCollider(
      std::unique_ptr<wp::collide::Collider> collider,
      std::function<void()> const& onWallHit = {});

  std::vector<wp::collide::StaticLine> const& getLines() const;

  void clearLines();

  void addLine(wp::Vector2 const& v0, wp::Vector2 const& v1, uint32_t index);
};
