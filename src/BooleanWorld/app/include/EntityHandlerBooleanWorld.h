#pragma once

#include <vector>

#include <applib/EntityHandler.h>
#include <applib/AnimationDatabase.h>

#include <core/World.h>

#include "InputOptions.h"
#include "Platform.h"

class EntityHandlerBooleanWorld : public applib::EntityHandler {
  std::shared_ptr<applib::AnimationDatabase> mAnimationDatabase;

  bool mInputEnabled;

  bw::app::InputOptions mInputOptions;

  // Set once per frame by StatePlayBooleanWorld (which alone knows the
  // player's liquid submersion) before either peekInput call this frame -
  // once here for wall-collision prediction, once again inside updateImpl for
  // the actual movement.
  float mSpeedMultiplier{1.0f};

  // Set alongside mSpeedMultiplier. While true, forward/back input moves
  // along the full pitched view direction (fly controls) rather than the
  // horizontal projection alone.
  bool mSwimming{false};

private:
  void updateVisual(applib::Entity* entity, float frameTime);

  std::string getPrototypeName(int type) override;

  void setupImpl(applib::Entity* entity) override;

  void destroyImpl(applib::Entity* entity) override;

  bool updateImpl(applib::Entity* entity, bool inputControlled, float frameTime) override;

public:
  explicit EntityHandlerBooleanWorld(std::shared_ptr<applib::AnimationDatabase> animationDatabase, bw::app::InputOptions const& inputOptions = {});

  void enableInput(bool enable);

  bool isInputEnabled() const;

  bw::app::InputOptions const& getInputOptions() const;

  void setInputOptions(bw::app::InputOptions const& inputOptions);

  void setSpeedMultiplier(float multiplier);

  void setSwimming(bool swimming);

  // verticalEffort is non-zero only while swimming (see setSwimming): the
  // fraction of forward/back input the pitched view direction turns into
  // vertical movement, in -1 to 1. Bare effort rather than a speed, because
  // what it achieves depends on the liquid being swum through, which this
  // handler knows nothing about.
  void peekInput(applib::Entity const& entity, wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float* curPitch, float* newPitch, wp::Vector2* velocity, float* verticalEffort, float frameTime) const;

  bool update(applib::Entity* entity, bool controlActive, float frameTime) override;
};