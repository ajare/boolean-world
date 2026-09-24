#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <willpower/common/BoundingBox.h>

#include <willpower/collide/Simulation.h>

class WorldCollisionSim : public wp::collide::Simulation {
public:
  enum class PortalLineResponse { Ignore, Block, Traverse };
  using PortalHitCallback = std::function<PortalLineResponse(
      wp::collide::SweepResult*, uint32_t portalLineIndex)>;

  enum class PlayerMovementSegmentType { Swept, PortalRelocation };

  struct PlayerMovementSegment {
    wp::Vector2 from;
    wp::Vector2 to;
    PlayerMovementSegmentType type{PlayerMovementSegmentType::Swept};
  };

private:
  struct MovementCandidate {
    wp::Vector2 oldPosition;
    wp::Vector2 newPosition;
    wp::Vector2 portalSourcePosition;
    bool portalRelocation{false};
  };

  PortalHitCallback mPortalHitCallback;
  uint32_t mPortalLineCount{0};
  wp::collide::Collider* mPlayerCollider{nullptr};
  bool mTracingUpdate{false};
  wp::Vector2 mUpdateStart;
  std::vector<MovementCandidate> mMovementCandidates;
  std::vector<PlayerMovementSegment> mPlayerMovementTrace;

  void recordMovementCandidate(
      wp::collide::SweepResult const& result, bool portalRelocation);
  void finishMovementTrace();

  bool sweepAgainstStaticLine(
      wp::collide::Collider const* collider,
      wp::Vector2 const& desiredPosition,
      wp::collide::StaticLine const& line, float* time) const override;

  void getLineIndices(
      wp::BoundingBox const& bounds,
      std::vector<uint32_t>& indices) const override;

public:
  explicit WorldCollisionSim(void* userObj = nullptr);

  void addSlidingCollider(
      std::unique_ptr<wp::collide::Collider> collider,
      std::function<void()> const& onWallHit = {});

  // Resolves the player's requested movement and replaces the previous trace.
  // Swept segments contain only movement actually travelled by the player
  // centre; Portal jumps are separate relocation segments.
  void update(float frameTime);

  std::vector<PlayerMovementSegment> const& getPlayerMovementTrace() const;

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
