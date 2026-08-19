#include <iostream>
#include <memory>
#include <stdexcept>

#include <core/BinarySerializer.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void newWorldHasExactlyOneLayerAtIndexZero() {
  bw::core::World world(100.0f, 10.0f);

  require(world.getNumLayers() == 1, "a newly constructed World did not have exactly one Layer");
  require(world.getLayers()[0]->getId() == 0, "a newly constructed World's default Layer did not have id 0");
  require(world.getActiveLayerIndex() == 0, "a newly constructed World's active Layer index was not 0");
  require(world.getActiveLayer() == world.getLayers()[0],
          "getActiveLayer() did not return the World's only Layer");
}

void copyingAWorldDeepCopiesItsLayersAndResetsActiveIndex() {
  bw::core::World source(100.0f, 10.0f);

  bw::core::World copy(source);

  require(copy.getNumLayers() == 1, "a copied World did not preserve its Layer count");
  require(copy.getLayers()[0] != source.getLayers()[0],
          "a copied World shared a Layer pointer with its source instead of deep-copying");
  require(copy.getActiveLayerIndex() == 0, "a copied World's active Layer index was not reset to 0");
}

void deserializingAWorldAlwaysResetsTheActiveLayerIndex() {
  std::string const path = "world_layer_seeding_tests.world";

  {
    bw::core::World world(100.0f, 10.0f);
    auto rect = new bw::core::RectanglePolygon(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        1.0f);
    rect->setSize(10.0f, 10.0f);
    world.addPrimitive(rect);

    std::shared_ptr<bw::core::Serializer> ser(bw::core::BinarySerializer::toFile(path));
    auto workData = bw::core::SerializationWorkData{};
    world.serialize(ser, workData);
    ser->serialize();
  }

  bw::core::World loaded(100.0f, 10.0f);
  {
    std::shared_ptr<bw::core::Serializer> ser(bw::core::BinarySerializer::fromFile(path));
    ser->deserialize();
    auto workData = bw::core::SerializationWorkData{};
    workData.accelGridSize = 10.0f;
    require(loaded.deserialize(ser, workData), "a World failed to deserialize from a .world binary file");
  }

  require(loaded.getNumLayers() == 1, "a deserialized World did not have exactly one Layer");
  require(loaded.getActiveLayerIndex() == 0, "a deserialized World's active Layer index was not reset to 0");

  std::remove(path.c_str());
}

}  // namespace

int main() {
  try {
    newWorldHasExactlyOneLayerAtIndexZero();
    copyingAWorldDeepCopiesItsLayersAndResetsActiveIndex();
    deserializingAWorldAlwaysResetsTheActiveLayerIndex();
    std::cout << "World auto-seeds a default Layer and keeps its active Layer index unserialized\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
