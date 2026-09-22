#pragma once

#include <functional>
#include <memory>

#include <willpower/common/BoundingBox.h>

#include <willpower/collide/Simulation.h>

class WorldCollisionSim : public wp::collide::Simulation {
public:
  enum class PortalLineResponse { Ignore, Block, Traverse };
  using PortalHitCallback = std::function<PortalLineResponse(
      wp::collide::SweepResult*, uint32_t portalLineIndex)>;

private:
  PortalHitCallback mPortalHitCallback;
  uint32_t mPortalLineCount{0};

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

  void setPortalHitCallback(PortalHitCallback callback);

  void addLine(wp::Vector2 const& v0, wp::Vector2 const& v1, uint32_t index);

  // Portal spans are special swept planes. Ignore makes the span an opening,
  // Block applies the ordinary sliding wall response, and Traverse supplies a
  // transformed SweepResult whose movementLeft is recursively consumed.
  uint32_t addPortalLine(
      wp::Vector2 const& v0, wp::Vector2 const& v1);
};
