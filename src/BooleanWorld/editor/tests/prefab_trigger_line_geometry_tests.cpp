#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/World.h>
#include <core/WorldTriggerLine.h>

#include "Defines.h"
#include "Document.h"
#include "Tiled.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();

void openTiledPrefabFile(std::string const&, std::shared_ptr<bw::core::World>) {
}

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

void prefabPlacementIndexesTransformedTriggerLineBounds() {
  bw::core::World prefab(1000.0f, 100.0f);
  prefab.addTriggerLine(new bw::core::WorldTriggerLine(
      {-120.0f, -100.0f}, {-80.0f, -100.0f}));

  editor::Document document;
  document.newDoc();
  document.addPrefabInstance(&prefab, 0, 0, 90.0f);

  auto world = document.getWorld();
  require(world->getNumTriggerLines() == 1,
          "prefab placement did not copy its trigger line");

  auto* placed = world->getTriggerLine(0);
  auto const offset = wp::Vector2(
      BW_PLAYER_RADIUS * BW_PREFAB_PLAYER_RATIO * 0.5f,
      BW_PLAYER_RADIUS * BW_PREFAB_PLAYER_RATIO * 0.5f);
  auto const transformedCentre = offset + wp::Vector2(100.0f, -100.0f);
  require(contains(world->findTriggerLines(
                       {{transformedCentre.x - 2.0f, transformedCentre.y - 25.0f}, {4.0f, 50.0f}}),
                   placed),
          "prefab trigger line was not indexed at its transformed bounds");
  require(!contains(world->findTriggerLines(
                        {{-125.0f, -102.0f}, {50.0f, 4.0f}}),
                    placed),
          "prefab trigger line retained lookup bounds from prefab space");
}

}  // namespace

int main() {
  try {
    prefabPlacementIndexesTransformedTriggerLineBounds();
    std::cout << "Prefab trigger-line bounds are transformed before indexing\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
