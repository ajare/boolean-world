#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <core/CirclePolygon.h>
#include <core/CircleSegmentPolygon.h>
#include <core/CoreException.h>
#include <core/RectanglePolygon.h>
#include <core/RegularPolygon.h>
#include <core/TorusPolygon.h>
#include <core/TorusSegmentPolygon.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {

using bw::core::CirclePolygon;
using bw::core::CircleSegmentPolygon;
using bw::core::Primitive;
using bw::core::RectanglePolygon;
using bw::core::RegularPolygon;
using bw::core::SerializationWorkData;
using bw::core::TorusPolygon;
using bw::core::TorusSegmentPolygon;
using bw::core::World;
using Operation = Primitive::Operation;
using FillRule = Primitive::FillRule;

constexpr Operation Union = Operation::Union;
constexpr FillRule NonZero = FillRule::NonZero;
constexpr float MinimumResolution = 3.0f / 64.0f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void requireDomainError(Function&& function, std::string const& message) {
  try {
    std::forward<Function>(function)();
  } catch (bw::core::CoreException const&) {
    return;
  }
  throw std::runtime_error(message);
}

void regularPolygonValidatesConstructorsAndSetters() {
  RegularPolygon minimum(Union, NonZero, 3);
  RegularPolygon maximum(Union, NonZero, 1024);
  require(minimum.getNumSides() == 3 && maximum.getNumSides() == 1024,
          "regular polygon rejected a documented side-count boundary");

  requireDomainError([] { RegularPolygon invalid(Union, NonZero, 2); },
                     "regular polygon constructor accepted fewer than three sides");
  requireDomainError([] { RegularPolygon invalid(Union, NonZero, 1025); },
                     "regular polygon constructor accepted more than the contour vertex limit");

  minimum.setNumSides(4);
  requireDomainError([&] { minimum.setNumSides(2); },
                     "regular polygon setter accepted fewer than three sides");
  require(minimum.getNumSides() == 4,
          "failed regular polygon setter changed the authored side count");
}

void circleValidatesConstructorsAndSetters() {
  CirclePolygon minimum(Union, NonZero, MinimumResolution);
  CirclePolygon maximum(Union, NonZero, 1.0f);
  require(minimum.getNumSides() == 3 && maximum.getNumSides() == 64,
          "circle rejected a documented resolution boundary");

  float const belowMinimum = std::nextafter(MinimumResolution, 0.0f);
  for (float invalid : {belowMinimum, -1.0f, 1.01f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError([&] { CirclePolygon circle(Union, NonZero, invalid); },
                       "circle constructor accepted an invalid resolution");
    requireDomainError([&] { maximum.setResolution(invalid); },
                       "circle setter accepted an invalid resolution");
  }

  requireDomainError([&] { maximum.setNumSides(2); },
                     "circle side-count setter accepted fewer than three vertices");
  requireDomainError([&] { maximum.setNumSides(65); },
                     "circle side-count setter accepted a resolution above one");
  require(maximum.getResolution() == 1.0f && maximum.getNumSides() == 64,
          "failed circle setter changed authored geometry");
}

void circleSegmentValidatesConstructorsAndSetters() {
  CircleSegmentPolygon minimum(Union, NonZero, 0.01f, MinimumResolution);
  CircleSegmentPolygon maximum(Union, NonZero, 360.0f, 1.0f);
  require(minimum.getNumSides() == 3 && maximum.getNumSides() == 64,
          "circle segment rejected a documented parameter boundary");
  require(minimum.getVertices().front().front().size() >= 3,
          "minimum circle segment did not produce a usable contour");

  float const belowMinimum = std::nextafter(MinimumResolution, 0.0f);
  for (float invalid : {belowMinimum, -1.0f, 1.01f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError(
        [&] { CircleSegmentPolygon segment(Union, NonZero, 90.0f, invalid); },
        "circle segment constructor accepted an invalid resolution");
    requireDomainError([&] { maximum.setResolution(invalid); },
                       "circle segment setter accepted an invalid resolution");
  }

  for (float invalid : {0.0f, 360.01f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError(
        [&] { CircleSegmentPolygon segment(Union, NonZero, invalid, 1.0f); },
        "circle segment constructor accepted an invalid arc length");
    requireDomainError([&] { maximum.setArcLength(invalid); },
                       "circle segment setter accepted an invalid arc length");
  }

  requireDomainError([&] { maximum.setNumSides(2); },
                     "circle segment side-count setter accepted fewer than three vertices");
  require(maximum.getArcLength() == 360.0f && maximum.getResolution() == 1.0f &&
              maximum.getNumSides() == 64,
          "failed circle segment setter changed authored geometry");
}

void torusValidatesConstructorsAndSetters() {
  TorusPolygon minimum(Union, NonZero, 0.01f, MinimumResolution);
  TorusPolygon maximum(Union, NonZero, 0.99f, 1.0f);
  require(minimum.getNumSides() == 3 && maximum.getNumSides() == 64,
          "torus rejected a documented parameter boundary");

  float const belowMinimumResolution = std::nextafter(MinimumResolution, 0.0f);
  for (float invalid : {0.0f, belowMinimumResolution, -1.0f, 1.01f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError([&] { TorusPolygon torus(Union, NonZero, 0.5f, invalid); },
                       "torus constructor accepted an invalid resolution");
    requireDomainError([&] { maximum.setResolution(invalid); },
                       "torus setter accepted an invalid resolution");
  }

  for (float invalid : {0.0f, -0.01f, 0.009f, 1.0f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError([&] { TorusPolygon torus(Union, NonZero, invalid, 1.0f); },
                       "torus constructor accepted an invalid thickness");
    requireDomainError([&] { maximum.setThickness(invalid); },
                       "torus setter accepted an invalid thickness");
  }

  requireDomainError([&] { maximum.setNumSides(2); },
                     "torus side-count setter accepted fewer than three vertices");
  requireDomainError([&] { maximum.setNumSides(65); },
                     "torus side-count setter accepted a resolution above one");
  require(maximum.getThickness() == 0.99f && maximum.getResolution() == 1.0f &&
              maximum.getNumSides() == 64,
          "failed torus setter changed authored geometry");
}

void torusSegmentValidatesConstructorsAndSetters() {
  TorusSegmentPolygon minimum(
      Union, NonZero, 0.01f, 0.01f, MinimumResolution);
  TorusSegmentPolygon maximum(Union, NonZero, 0.99f, 360.0f, 1.0f);
  require(minimum.getNumSides() == 3 && maximum.getNumSides() == 64,
          "torus segment rejected a documented parameter boundary");
  require(minimum.getVertices().front().front().size() == 8,
          "minimum torus segment did not produce a bounded contour");

  for (float invalid : {0.0f, -1.0f, 360.01f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError(
        [&] { TorusSegmentPolygon segment(Union, NonZero, 0.5f, invalid, 1.0f); },
        "torus segment constructor accepted an invalid arc length");
    requireDomainError([&] { maximum.setArcLength(invalid); },
                       "torus segment setter accepted an invalid arc length");
  }

  for (float invalid : {0.0f, -1.0f, 0.009f, 1.0f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError(
        [&] { TorusSegmentPolygon segment(Union, NonZero, invalid, 90.0f, 1.0f); },
        "torus segment constructor accepted an invalid thickness");
    requireDomainError([&] { maximum.setThickness(invalid); },
                       "torus segment setter accepted an invalid thickness");
  }

  float const belowMinimumResolution = std::nextafter(MinimumResolution, 0.0f);
  for (float invalid : {0.0f, belowMinimumResolution, -1.0f, 1.01f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError(
        [&] { TorusSegmentPolygon segment(Union, NonZero, 0.5f, 90.0f, invalid); },
        "torus segment constructor accepted an invalid resolution");
    requireDomainError([&] { maximum.setResolution(invalid); },
                       "torus segment setter accepted an invalid resolution");
  }

  requireDomainError([&] { maximum.setNumSides(2); },
                     "torus segment side-count setter accepted fewer than three vertices");
  require(maximum.getThickness() == 0.99f && maximum.getArcLength() == 360.0f &&
              maximum.getResolution() == 1.0f && maximum.getNumSides() == 64,
          "failed torus segment setter changed authored geometry");
}

void rectangleValidatesConstructorsAndSetters() {
  RectanglePolygon minimum(Union, NonZero, 1.0f);
  RectanglePolygon maximum(Union, NonZero, 10.0f);
  require(minimum.getXyRatio() == 1.0f && maximum.getXyRatio() == 10.0f,
          "rectangle rejected a documented ratio boundary");

  for (float invalid : {0.99f, 10.01f, 0.0f, -1.0f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    requireDomainError([&] { RectanglePolygon rectangle(Union, NonZero, invalid); },
                       "rectangle constructor accepted an invalid ratio");
    requireDomainError([&] { maximum.setXyRatio(invalid); },
                       "rectangle setter accepted an invalid ratio");
  }
  require(maximum.getXyRatio() == 10.0f,
          "failed rectangle setter changed the authored ratio");
}

std::string serializeWorld(std::unique_ptr<Primitive> primitive) {
  World world(100.0f, 10.0f);
  world.addPrimitive(primitive.release());
  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  SerializationWorkData workData;
  world.serialize(writer, workData);
  return writer->getSerializedString();
}

std::string replaceScalar(std::string yaml, std::string const& key,
                          std::string const& value) {
  std::string const marker = key + ": ";
  auto const position = yaml.rfind(marker);
  require(position != std::string::npos,
          std::format("serialized primitive did not contain {}", key));
  auto const valueStart = position + marker.size();
  auto const valueEnd = yaml.find('\n', valueStart);
  yaml.replace(valueStart, valueEnd - valueStart, value);
  return yaml;
}

void requireMalformedWorldRejected(std::string const& yaml,
                                   std::string const& expectedError) {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  reader->deserialize();

  World target(100.0f, 10.0f);
  SerializationWorkData workData{10.0f};
  require(!target.deserialize(reader, workData),
          "world with an invalid primitive parameter deserialized");
  for (auto const& error : target.getDeserializationErrors()) {
    if (error.find(expectedError) != std::string::npos) {
      return;
    }
  }
  throw std::runtime_error(
      std::format("malformed world did not report an error containing '{}'", expectedError));
}

void malformedFilesRejectEveryAffectedPrimitive() {
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<RegularPolygon>(Union, NonZero, 3)),
                    "numSides", "2"),
      "side count");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<CirclePolygon>(Union, NonZero, 1.0f)),
                    "resolution", ".nan"),
      "Circle resolution");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<CircleSegmentPolygon>(
                        Union, NonZero, 90.0f, 1.0f)),
                    "resolution", "0"),
      "Circle segment resolution");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<RectanglePolygon>(Union, NonZero, 1.0f)),
                    "xyRatio", "0"),
      "Rectangle width-to-height ratio");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<TorusPolygon>(
                        Union, NonZero, 0.5f, 1.0f)),
                    "resolution", ".inf"),
      "Torus resolution");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<TorusPolygon>(
                        Union, NonZero, 0.5f, 1.0f)),
                    "thickness", "0"),
      "Torus thickness");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<TorusSegmentPolygon>(
                        Union, NonZero, 0.5f, 90.0f, 1.0f)),
                    "thickness", ".nan"),
      "Torus segment thickness");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<TorusSegmentPolygon>(
                        Union, NonZero, 0.5f, 90.0f, 1.0f)),
                    "arcLength", "-1"),
      "Torus segment arc length");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<TorusSegmentPolygon>(
                        Union, NonZero, 0.5f, 90.0f, 1.0f)),
                    "resolution", "0"),
      "Torus segment resolution");
  requireMalformedWorldRejected(
      replaceScalar(serializeWorld(std::make_unique<TorusSegmentPolygon>(
                        Union, NonZero, 0.5f, 90.0f, 1.0f)),
                    "numSides", "0"),
      "Torus segment resolution does not match its side count");
}

}  // namespace

int main() {
  try {
    regularPolygonValidatesConstructorsAndSetters();
    circleValidatesConstructorsAndSetters();
    circleSegmentValidatesConstructorsAndSetters();
    torusValidatesConstructorsAndSetters();
    torusSegmentValidatesConstructorsAndSetters();
    rectangleValidatesConstructorsAndSetters();
    malformedFilesRejectEveryAffectedPrimitive();
    std::cout << "Primitive geometry parameters are validated consistently\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
