#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/SuperformulaPolygon.h>
#include <core/TorusPolygon.h>
#include <core/TorusSegmentPolygon.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireBoundsContainVertices(bw::core::Primitive const& primitive) {
  wp::Vector2 minimum, maximum;
  primitive.getBounds().getExtents(minimum, maximum);

  for (auto const& complexPolygon : primitive.getVertices()) {
    for (auto const& contour : complexPolygon) {
      for (auto const& vertex : contour) {
        require(vertex.p.x >= minimum.x - Epsilon && vertex.p.x <= maximum.x + Epsilon &&
                    vertex.p.y >= minimum.y - Epsilon && vertex.p.y <= maximum.y + Epsilon,
                "primitive bounds did not contain a contour vertex");
      }
    }
  }
}

std::vector<std::unique_ptr<bw::core::Primitive>> makePrimitives() {
  float superformulaValues[] = {2.0f, 1.5f, 5.0f, 1.0f, 1.0f, 1.0f};
  std::vector<std::unique_ptr<bw::core::Primitive>> primitives;
  primitives.emplace_back(new bw::core::TorusPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      0.25f,
      1.0f));
  primitives.emplace_back(new bw::core::TorusSegmentPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      0.25f,
      180.0f,
      1.0f));
  primitives.emplace_back(new bw::core::SuperformulaPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f,
      superformulaValues));

  for (auto& primitive : primitives) {
    primitive->setSize(1.0f, 1.0f);
  }
  return primitives;
}

void primitiveBoundsSurviveAllPrimitivePaths() {
  auto primitives = makePrimitives();

  for (auto const& primitive : primitives) {
    auto copy = std::unique_ptr<bw::core::Primitive>(primitive->copy());
    copy->calculateBounds();
  }

  bw::core::World source(100.0f, 10.0f);
  for (auto& primitive : primitives) {
    source.addPrimitive(primitive.release());
  }

  for (uint32_t index = 0; index < source.getNumPrimitives(); ++index) {
    requireBoundsContainVertices(*source.getPrimitive(index));
  }

  auto const& superformulaBounds = source.getPrimitive(2)->getBounds();
  wp::Vector2 superformulaMinimum, superformulaMaximum;
  superformulaBounds.getExtents(superformulaMinimum, superformulaMaximum);
  require(superformulaMaximum.x - superformulaMinimum.x > 2.0f,
          "superformula bounds did not use its generated contour");

  std::string const path = "primitive_bounds_tests.yaml";
  auto writer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toFile(path));
  bw::core::SerializationWorkData writeWorkData;
  source.serialize(writer, writeWorkData);
  writer->serialize();

  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromFile(path));
  reader->deserialize();

  bw::core::World target;
  bw::core::SerializationWorkData readWorkData{10.0f};
  require(target.deserialize(reader, readWorkData),
          "world containing generated primitives did not deserialize");
  require(target.getNumPrimitives() == 3,
          "serialized world did not retain every generated primitive");
  for (uint32_t index = 0; index < target.getNumPrimitives(); ++index) {
    requireBoundsContainVertices(*target.getPrimitive(index));
  }

  std::remove(path.c_str());
}

}  // namespace

int main() {
  try {
    primitiveBoundsSurviveAllPrimitivePaths();
    std::cout << "Generated primitive bounds cover construction, copying, and serialization\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "A non-standard exception escaped a primitive bounds path\n";
    return 1;
  }
}
