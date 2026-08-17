#include <cmath>
#include <iostream>

#include "PlayerView.h"
#include "ReactiveCamera.h"

namespace {

constexpr float Epsilon = 0.0001f;

bool near(float actual, float expected) {
  return std::abs(actual - expected) < Epsilon;
}

int fail(char const* message) {
  std::cerr << message << '\n';
  return 1;
}

class TestReactiveCamera : public ReactiveCamera {
public:
  using ReactiveCamera::ReactiveCamera;

  bool isDirty() const {
    return mDirty;
  }
};

}  // namespace

int main() {
  {
    auto movement = bw::app::playerMovement({0.0f, 1.0f}, 0.0f);
    if (!near(movement.x, 0.0f) || !near(movement.y, -1.0f)) {
      return fail("forward input does not follow a zero-yaw camera");
    }
  }

  {
    auto movement = bw::app::playerMovement({1.0f, 0.0f}, 0.0f);
    if (!near(movement.x, 1.0f) || !near(movement.y, 0.0f)) {
      return fail("right input does not follow a zero-yaw camera");
    }
  }

  {
    auto forward = bw::app::playerMovement({0.0f, 1.0f}, 90.0f);
    auto right = bw::app::playerMovement({1.0f, 0.0f}, 90.0f);
    if (!near(forward.x, 1.0f) || !near(forward.y, 0.0f) ||
        !near(right.x, 0.0f) || !near(right.y, 1.0f)) {
      return fail("movement axes do not rotate with player yaw");
    }
  }

  if (!near(bw::app::applyMouseYaw(350.0f, 20.0f, 1.0f), 10.0f)) {
    return fail("rightward mouse input does not increase player yaw");
  }

  if (!near(bw::app::applyMouseYaw(0.0f, 20.0f, 2.0f), 40.0f) ||
      !near(bw::app::applyMouseYaw(0.0f, 20.0f, 0.25f), 5.0f)) {
    return fail("mouse sensitivity does not scale player yaw");
  }

  if (!near(bw::app::applyMousePitch(0.0f, 10.0f, 1.0f), 10.0f) ||
      !near(bw::app::applyMousePitch(0.0f, 10.0f, 3.0f), 30.0f)) {
    return fail("mouse sensitivity does not scale player pitch");
  }

  if (!near(bw::app::applyMousePitch(0.0f, 400.0f, 2.0f), bw::app::PitchLimit) ||
      !near(bw::app::applyMousePitch(0.0f, -400.0f, 2.0f), -bw::app::PitchLimit)) {
    return fail("a sensitive mouse turns the view past the pitch limit");
  }

  if (!near(bw::app::worldViewAngle(0.0f), 180.0f) ||
      !near(bw::app::worldViewAngle(90.0f), 90.0f)) {
    return fail("core view angle does not match player camera yaw");
  }

  {
    constexpr float yaw = 35.0f;
    auto movement = bw::app::playerMovement({0.0f, 1.0f}, yaw);
    auto viewDirection = wp::Vector2::fromAngle(
        bw::app::worldViewAngle(yaw), wp::Clockwise);
    if (!near(viewDirection.x, movement.x) ||
        !near(viewDirection.y, movement.y)) {
      return fail("debug view direction does not match forward movement");
    }
  }

  {
    if (!near(bw::app::minimapRadius(15.0f, {2.0f, 3.0f}), 30.0f)) {
      return fail("debug minimap radius does not follow horizontal view scale");
    }
  }

  {
    auto player = bw::app::minimapPosition({10.0f, 20.0f}, {0.0f, 0.0f}, {1.0f, 1.0f});
    auto forward = bw::app::minimapPosition({10.0f, 19.0f}, {0.0f, 0.0f}, {1.0f, 1.0f});
    auto right = bw::app::minimapPosition({11.0f, 20.0f}, {0.0f, 0.0f}, {1.0f, 1.0f});
    if (!(forward.y < player.y) || !(right.x > player.x)) {
      return fail("minimap axes do not agree with camera forward and right");
    }
  }

  {
    ReactiveCamera camera({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 75.0f, 1.0f);
    auto const& direction = camera.getDirection();
    if (!near(direction.x, 0.0f) || !near(direction.y, 0.0f) ||
        !near(direction.z, -1.0f)) {
      return fail("zero-yaw camera does not look along world -Y");
    }
  }

  {
    ReactiveCamera camera({0.0f, 0.0f, 0.0f}, 90.0f, 0.0f, 75.0f, 1.0f);
    auto const& direction = camera.getDirection();
    if (!near(direction.x, 1.0f) || !near(direction.y, 0.0f) ||
        !near(direction.z, 0.0f)) {
      return fail("positive camera yaw does not turn right");
    }
  }

  {
    TestReactiveCamera camera({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 75.0f, 1.0f);
    camera.getDirection();
    camera.setPosition({1.0f, 2.0f, 3.0f});
    if (!camera.isDirty()) {
      return fail("position changes do not invalidate the camera");
    }
  }

  return 0;
}
