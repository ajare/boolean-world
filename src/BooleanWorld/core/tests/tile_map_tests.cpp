#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <core/BinarySerializer.h>
#include <core/CoreException.h>
#include <core/DefineTileMaps.h>
#include <core/Layer.h>
#include <core/LayerBuildStep.h>
#include <core/SerializationWorkData.h>
#include <core/TileMap.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireCoreException(Function&& function, char const* message) {
  try {
    function();
  } catch (bw::core::CoreException const&) {
    return;
  }
  throw std::runtime_error(message);
}

void defaultsAndCellAccessAreBinaryAndBounded() {
  bw::core::DefineTileMaps definitions;
  require(definitions.getNumTileMaps() == 1,
          "DefineTileMaps did not default to one TileMap");
  require(definitions.getMapSize() == 256,
          "DefineTileMaps did not default to Map size 256");
  require(definitions.getCellSize() == 32,
          "DefineTileMaps did not default to cell size 32");
  auto* map = definitions.getTileMap(0);
  require(map->getIndex() == 0, "the first TileMap did not have index zero");
  require(map->getWidth() == 8 && map->getHeight() == 8,
          "TileMap default dimensions were not 8x8");
  require(map->getCell(0, 0) == 0 && map->getCell(7, 7) == 0,
          "TileMap cells did not default to zero");

  map->setCell(7, 7, 1);
  require(map->getCell(7, 7) == 1, "setCell did not set a cell");
  map->toggleCell(7, 7);
  require(map->getCell(7, 7) == 0, "toggleCell did not toggle a cell");
  requireCoreException([&] { map->setCell(0, 0, 2); },
                       "TileMap accepted a non-binary value");
  requireCoreException([&] { (void)map->getCell(8, 0); },
                       "TileMap accepted an out-of-range x coordinate");
  requireCoreException([&] { (void)map->getCell(0, 8); },
                       "TileMap accepted an out-of-range y coordinate");
}

void countIsBoundedAndIndicesFollowPosition() {
  bw::core::DefineTileMaps definitions;
  definitions.setNumTileMaps(16);
  require(definitions.getNumTileMaps() == 16,
          "DefineTileMaps did not grow to sixteen TileMaps");
  for (uint32_t i = 0; i < 16; ++i) {
    require(definitions.getTileMap(i)->getIndex() == i,
            "a TileMap index did not match its position");
  }
  definitions.getTileMap(2)->setCell(0, 0, 1);
  definitions.setNumTileMaps(3);
  require(definitions.getTileMap(2)->getCell(0, 0) == 1,
          "reducing the count changed a retained TileMap");
  definitions.setNumTileMaps(4);
  require(definitions.getTileMap(3)->getCell(0, 0) == 0,
          "increasing the count did not append an empty TileMap");
  requireCoreException([&] { definitions.setNumTileMaps(0); },
                       "DefineTileMaps accepted zero TileMaps");
  requireCoreException([&] { definitions.setNumTileMaps(17); },
                       "DefineTileMaps accepted more than sixteen TileMaps");
  requireCoreException([&] { (void)definitions.getTileMap(4); },
                       "DefineTileMaps accepted an out-of-range index");
}

void changingEitherSizeClearsTheMap() {
  bw::core::DefineTileMaps definitions;
  definitions.setNumTileMaps(2);
  definitions.getTileMap(0)->setCell(1, 1, 1);
  definitions.getTileMap(1)->setCell(1, 1, 1);
  definitions.setMapSize(128);
  auto* map = definitions.getTileMap(0);
  require(map->getWidth() == 4 && map->getCell(1, 1) == 0 &&
                           definitions.getTileMap(1)->getCell(1, 1) == 0,
          "changing Map size did not clear every TileMap");
  map->setCell(1, 1, 1);
  definitions.getTileMap(1)->setCell(1, 1, 1);
  definitions.setCellSize(8);
  map = definitions.getTileMap(0);
  require(map->getWidth() == 16 && map->getCell(1, 1) == 0 &&
              definitions.getTileMap(1)->getCell(1, 1) == 0,
          "changing cell size did not clear every TileMap");

  requireCoreException([&] { definitions.setMapSize(32); },
                       "DefineTileMaps accepted an invalid Map size");
  requireCoreException([&] { definitions.setCellSize(64); },
                       "DefineTileMaps accepted an invalid cell size");
}

template <typename Writer, typename Reader>
void roundTrip(char const* path, Writer writer, Reader reader) {
  bw::core::Layer source(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefineTileMaps;
  definitions->setName("layout");
  definitions->setMapSize(64);
  definitions->setCellSize(2);
  definitions->setNumTileMaps(2);
  auto* map = definitions->getTileMap(0);
  map->setCell(0, 0, 1);
  map->setCell(31, 31, 1);
  definitions->getTileMap(1)->setCell(4, 5, 1);
  source.addStep(definitions);

  {
    std::shared_ptr<bw::core::Serializer> serializer(writer(path));
    bw::core::SerializationWorkData workData;
    source.serialize(serializer, workData);
    serializer->serialize();
  }

  bw::core::Layer loaded;
  {
    std::shared_ptr<bw::core::Serializer> serializer(reader(path));
    serializer->deserialize();
    bw::core::SerializationWorkData workData;
    workData.accelGridSize = 10.0f;
    require(loaded.deserialize(serializer, workData),
            "Layer containing DefineTileMaps failed to deserialize");
  }
  auto* loadedDefinitions =
      dynamic_cast<bw::core::DefineTileMaps*>(loaded.getStep(1));
  require(loadedDefinitions && loadedDefinitions->getName() == "layout",
          "DefineTileMaps type or name did not round-trip");
  require(loadedDefinitions->getMapSize() == 64 &&
              loadedDefinitions->getCellSize() == 2,
          "DefineTileMaps sizes did not round-trip");
  require(loadedDefinitions->getNumTileMaps() == 2,
          "DefineTileMaps count did not round-trip");
  auto const* loadedMap = loadedDefinitions->getTileMap(0);
  require(loadedMap->getCell(0, 0) == 1 &&
              loadedMap->getCell(31, 31) == 1 &&
              loadedMap->getCell(1, 1) == 0 &&
              loadedDefinitions->getTileMap(1)->getCell(4, 5) == 1,
          "TileMap cells did not round-trip");
  require(loaded.getNumPrimitives() == 0,
          "data-only DefineTileMaps produced Primitives");
  std::remove(path);
}

void legacyTileMapLoadsAsOneDefineTileMapsMap() {
  auto const* path = "legacy_tile_map_tests.layer.yaml";
  {
    std::ofstream file(path);
    file << R"(id: 0
name: test
minExtent: [-256, -256]
maxExtent: [256, 256]
nextStepId: 2
steps:
  - type: PrimitiveField
    id: 0
    enabled: 1
    name: ""
    primitives: []
  - type: TileMap
    id: 1
    enabled: 1
    name: layout
    mapSize: 64
    cellSize: 32
    cells: [1, 0, 0, 1]
triggerLines: []
)";
  }

  bw::core::Layer loaded;
  std::shared_ptr<bw::core::Serializer> serializer(
      bw::core::YamlSerializer::fromFile(path));
  serializer->deserialize();
  bw::core::SerializationWorkData workData;
  workData.accelGridSize = 10.0f;
  if (!loaded.deserialize(serializer, workData)) {
    for (auto const& error : loaded.getDeserializationErrors()) {
      std::cerr << error << '\n';
    }
    throw std::runtime_error("legacy TileMap step failed to deserialize");
  }
  auto const* definitions =
      dynamic_cast<bw::core::DefineTileMaps const*>(loaded.getStep(1));
  require(definitions && definitions->getType() == "DefineTileMaps" &&
              definitions->getNumTileMaps() == 1,
          "legacy TileMap did not migrate to one DefineTileMaps map");
  require(definitions->getTileMap(0)->getCell(0, 0) == 1 &&
              definitions->getTileMap(0)->getCell(1, 1) == 1,
          "legacy TileMap cells did not migrate");
  std::remove(path);
}

void tileMapRoundTrips() {
  roundTrip(
      "tile_map_tests.layer",
      [](char const* path) { return bw::core::BinarySerializer::toFile(path); },
      [](char const* path) { return bw::core::BinarySerializer::fromFile(path); });
  roundTrip(
      "tile_map_tests.layer.yaml",
      [](char const* path) { return bw::core::YamlSerializer::toFile(path); },
      [](char const* path) { return bw::core::YamlSerializer::fromFile(path); });
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    defaultsAndCellAccessAreBinaryAndBounded();
    countIsBoundedAndIndicesFollowPosition();
    changingEitherSizeClearsTheMap();
    tileMapRoundTrips();
    legacyTileMapLoadsAsOneDefineTileMapsMap();
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
