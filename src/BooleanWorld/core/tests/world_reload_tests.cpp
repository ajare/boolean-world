#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void reloadRecreatesAccelerationGrids() {
  std::string const path = "world_reload_tests.yaml";

  bw::core::World source(100.0f, 10.0f);
  source.addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f));
  source.addTriggerLine(new bw::core::WorldTriggerLine(
      0, {10.0f, 20.0f}, {30.0f, 40.0f}));

  auto writer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toFile(path));
  bw::core::SerializationWorkData writeWorkData;
  source.serialize(writer, writeWorkData);
  writer->serialize();

  bw::core::World target;
  for (int reload = 0; reload < 2; ++reload) {
    auto reader = std::shared_ptr<bw::core::Serializer>(
        bw::core::YamlSerializer::fromFile(path));
    reader->deserialize();

    bw::core::SerializationWorkData readWorkData{10.0f};
    require(target.deserialize(reader, readWorkData),
            "world reload failed");
  }

  int gridWidth = 0;
  require(target.getGridSettings(&gridWidth, nullptr, nullptr),
          "world reload did not recreate the primitive acceleration grid");
  require(gridWidth == 10,
          "world reload recreated the primitive acceleration grid with the wrong dimensions");
  require(!target.findPrimitives({{-50.0f, -50.0f}, {50.0f, 50.0f}}).empty(),
          "world reload did not register primitives in the recreated acceleration grid");
  require(!target.findTriggerLines({{0.0f, 0.0f}, {50.0f, 50.0f}}).empty(),
          "world reload did not register trigger lines in the recreated acceleration grid");

  std::remove(path.c_str());
}

}  // namespace

int main() {
  try {
    reloadRecreatesAccelerationGrids();
    std::cout << "World reload recreates acceleration grids\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
