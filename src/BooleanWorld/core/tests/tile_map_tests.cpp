#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <core/BinarySerializer.h>
#include <core/CoreException.h>
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
  bw::core::TileMap map;
  require(map.getMapSize() == 256, "TileMap did not default to Map size 256");
  require(map.getCellSize() == 32, "TileMap did not default to cell size 32");
  require(map.getWidth() == 8 && map.getHeight() == 8,
          "TileMap default dimensions were not 8x8");
  require(map.getCell(0, 0) == 0 && map.getCell(7, 7) == 0,
          "TileMap cells did not default to zero");

  map.setCell(7, 7, 1);
  require(map.getCell(7, 7) == 1, "setCell did not set a cell");
  map.toggleCell(7, 7);
  require(map.getCell(7, 7) == 0, "toggleCell did not toggle a cell");
  requireCoreException([&] { map.setCell(0, 0, 2); },
                       "TileMap accepted a non-binary value");
  requireCoreException([&] { (void)map.getCell(8, 0); },
                       "TileMap accepted an out-of-range x coordinate");
  requireCoreException([&] { (void)map.getCell(0, 8); },
                       "TileMap accepted an out-of-range y coordinate");
}

void changingEitherSizeClearsTheMap() {
  bw::core::TileMap map;
  map.setCell(1, 1, 1);
  map.setMapSize(128);
  require(map.getWidth() == 4 && map.getCell(1, 1) == 0,
          "changing Map size did not clear the TileMap");
  map.setCell(1, 1, 1);
  map.setCellSize(8);
  require(map.getWidth() == 16 && map.getCell(1, 1) == 0,
          "changing cell size did not clear the TileMap");

  requireCoreException([&] { map.setMapSize(32); },
                       "TileMap accepted an invalid Map size");
  requireCoreException([&] { map.setCellSize(64); },
                       "TileMap accepted an invalid cell size");
}

template <typename Writer, typename Reader>
void roundTrip(char const* path, Writer writer, Reader reader) {
  bw::core::Layer source(0, "test", 512.0f, 16.0f);
  auto* map = new bw::core::TileMap;
  map->setName("layout");
  map->setMapSize(64);
  map->setCellSize(2);
  map->setCell(0, 0, 1);
  map->setCell(31, 31, 1);
  source.addStep(map);

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
            "Layer containing TileMap failed to deserialize");
  }
  auto* loadedMap = dynamic_cast<bw::core::TileMap*>(loaded.getStep(1));
  require(loadedMap && loadedMap->getName() == "layout",
          "TileMap type or name did not round-trip");
  require(loadedMap->getMapSize() == 64 && loadedMap->getCellSize() == 2,
          "TileMap sizes did not round-trip");
  require(loadedMap->getCell(0, 0) == 1 &&
              loadedMap->getCell(31, 31) == 1 &&
              loadedMap->getCell(1, 1) == 0,
          "TileMap cells did not round-trip");
  require(loaded.getNumPrimitives() == 0,
          "a data-only TileMap produced Primitives");
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
    changingEitherSizeClearsTheMap();
    tileMapRoundTrips();
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
