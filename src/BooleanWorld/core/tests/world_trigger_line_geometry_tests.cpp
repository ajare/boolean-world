#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool contains(std::vector<bw::core::WorldTriggerLine*> const& lines,
              bw::core::WorldTriggerLine const* expected) {
  return std::find(lines.begin(), lines.end(), expected) != lines.end();
}

void markUnmodified(bw::core::WorldTriggerLine const& triggerLine) {
  auto serializer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData workData;
  triggerLine.serialize(serializer, workData);
}

void endpointMovementUpdatesGeometryLookupAndAuthoredState() {
  bw::core::World world(1000.0f, 100.0f);
  auto* triggerLine = new bw::core::WorldTriggerLine(
      0, {-410.0f, -10.0f}, {-390.0f, 10.0f});
  auto const index = world.addTriggerLine(triggerLine);
  markUnmodified(*triggerLine);

  world.setTriggerLinePoint(index, 0, {110.0f, -10.0f});

  require(triggerLine->getPoint(0) == wp::Vector2(110.0f, -10.0f) &&
              triggerLine->getPoint(1) == wp::Vector2(-390.0f, 10.0f),
          "endpoint movement did not commit both endpoint values coherently");
  require(triggerLine->getBounds().getMinExtent().x == -391.0f &&
              triggerLine->getBounds().getMaxExtent().x == 111.0f,
          "endpoint movement did not recalculate trigger-line bounds");
  require(contains(world.findTriggerLines({{100.0f, -20.0f}, {20.0f, 40.0f}}),
                   triggerLine),
          "endpoint movement was not indexed at its new bound");
  require(triggerLine->isModified(),
          "endpoint movement did not mark authored state modified");
}

void wholeLineMovementReplacesOldLookupBounds() {
  bw::core::World world(1000.0f, 100.0f);
  auto* triggerLine = new bw::core::WorldTriggerLine(
      0, {-410.0f, -10.0f}, {-390.0f, 10.0f});
  auto const index = world.addTriggerLine(triggerLine);
  markUnmodified(*triggerLine);

  world.moveTriggerLine(index, {500.0f, 100.0f});

  require(triggerLine->getPoint(0) == wp::Vector2(90.0f, 90.0f) &&
              triggerLine->getPoint(1) == wp::Vector2(110.0f, 110.0f),
          "whole-line movement did not update both endpoints");
  require(!contains(world.findTriggerLines({{-420.0f, -20.0f}, {40.0f, 40.0f}}),
                    triggerLine),
          "whole-line movement left a stale lookup entry at the old bounds");
  require(contains(world.findTriggerLines({{80.0f, 80.0f}, {40.0f, 40.0f}}),
                   triggerLine),
          "whole-line movement was not indexed at the new bounds");
  require(triggerLine->isModified(),
          "whole-line movement did not mark authored state modified");
}

}  // namespace

int main() {
  try {
    endpointMovementUpdatesGeometryLookupAndAuthoredState();
    wholeLineMovementReplacesOldLookupBounds();
    std::cout << "Trigger-line geometry updates are atomic\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
