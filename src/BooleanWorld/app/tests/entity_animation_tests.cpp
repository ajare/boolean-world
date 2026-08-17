#include <iostream>
#include <memory>
#include <stdexcept>

#include <willpower/application/resourcesystem/AnimationSetResource.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#define private public
#include "applib/AnimationDatabase.h"
#undef private

#include "EntityHandlerBooleanWorld.h"
#include "EntityType.h"

namespace {
void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::shared_ptr<applib::AnimationDatabase> makeAnimationDatabase(float frameTime) {
  auto database = std::make_shared<applib::AnimationDatabase>(nullptr);
  database->mEntries.push_back({"test", 0, 1, applib::AnimationDatabase::LoopStyle::Forwards});

  applib::AnimationDatabase::Frame frame{};
  frame.time = frameTime;
  database->mFrames.push_back(frame);
  return database;
}

void zeroDurationFrameDoesNotBlockAnimationUpdate() {
  auto database = makeAnimationDatabase(0.0f);
  EntityHandlerBooleanWorld handler(database);

  auto prototype = handler.registerPrototype("Player");
  handler.registerProtoComponent(prototype, applib::VisualSprite{0, 1, 0, 0.0f, {}});

  applib::Entity entity;
  handler.setup(&entity, static_cast<int>(EntityType::Player), {}, 0.0f);
  require(handler.update(&entity, false, 0.1f),
          "A zero-duration animation frame did not return from update");

  auto const& visual = handler.getEntityComponent<applib::VisualSprite>(entity);
  require(visual.frame == 0, "A zero-duration animation frame advanced unexpectedly");
  require(visual.timer == 0.1f, "A zero-duration animation frame consumed elapsed time");
}

void invalidAnimationFrameIndexIsRejected() {
  auto database = makeAnimationDatabase(0.1f);
  bool threw = false;
  try {
    database->getAnimationFrame(0, 1);
  } catch (std::exception const&) {
    threw = true;
  }
  require(threw, "An out-of-range animation frame index was accepted");
}
}  // namespace

int main() {
  try {
    zeroDurationFrameDoesNotBlockAnimationUpdate();
    invalidAnimationFrameIndexIsRejected();
    std::cout << "Entity animation regression tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
