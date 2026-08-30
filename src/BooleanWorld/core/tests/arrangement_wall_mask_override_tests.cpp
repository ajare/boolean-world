#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/PrimitivePropertySet.h>

namespace {
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::WallMaskOverride;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;

constexpr int64_t U = bw::core::arr::FixedPointUnitsPerWorldUnit;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

Contour rectangle(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

ArrangementPrimitive square(
    uint64_t priority, uint32_t id, PrimitivePropertySet properties = {}) {
  return {{rectangle(0, 0, 10 * U, 10 * U)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, priority, id, properties};
}

WallMaskOverride maskImage(
    std::string resource, uint8_t channel,
    std::array<float, WallMaskOverride::BlendParameterCount> parameters) {
  return WallMaskOverride::image(std::move(resource), channel, parameters);
}

void maskOnEdge(
    ArrangementPrimitive& primitive, size_t edge,
    WallMaskOverride const& value) {
  primitive.contourEdgeWallMaskOverrides.resize(1);
  primitive.contourEdgeWallMaskOverrides[0].resize(4);
  primitive.contourEdgeWallMaskOverrides[0][edge] = value;
}

std::vector<bw::core::arr::ArrangementWall> wallsFor(
    std::vector<ArrangementPrimitive> primitives) {
  return bw::core::arr::BuildArrangementWalls(
      *bw::core::arr::BuildArrangement(primitives));
}

size_t countMask(
    std::vector<bw::core::arr::ArrangementWall> const& walls,
    WallMaskOverride const& value) {
  return std::count_if(walls.begin(), walls.end(), [&](auto const& wall) {
    return wall.wallMaskOverride == value;
  });
}

void triStateAndFoldPrecedenceAreExplicit() {
  auto image = maskImage("mask/low.png", 0, {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f});
  auto highImage = maskImage("mask/high.png", 3, {1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.7f});
  auto low = square(10, 1);
  maskOnEdge(low, 0, image);
  auto highUnset = square(20, 2);
  // Deliberately put an explicit Unset in the input: it is not a winning
  // choice and must not clear the lower Image.
  maskOnEdge(highUnset, 0, WallMaskOverride::unset());
  auto highDisabled = square(20, 2);
  maskOnEdge(highDisabled, 0, WallMaskOverride::disabled());
  auto highMapped = square(20, 2);
  maskOnEdge(highMapped, 0, highImage);

  for (auto primitives : {std::vector<ArrangementPrimitive>{highUnset, low},
                          std::vector<ArrangementPrimitive>{low, highUnset}}) {
    auto walls = wallsFor(std::move(primitives));
    require(countMask(walls, image) == 1,
            "Unset did not preserve the lower-precedence Image independent of input order");
  }
  auto disabledWalls = wallsFor({low, highDisabled});
  require(countMask(disabledWalls, WallMaskOverride::disabled()) == 1,
          "Disabled did not explicitly suppress a lower Image");
  auto mappedWalls = wallsFor({highMapped, low});
  require(countMask(mappedWalls, highImage) == 1,
          "the highest-precedence Image did not win as one complete value");

  // The winning wall must carry every authored field as one complete value:
  // state, resource, channel, and the full blend-parameter array.
  auto found = std::find_if(mappedWalls.begin(), mappedWalls.end(), [&](auto const& wall) {
    return wall.wallMaskOverride == highImage;
  });
  require(found != mappedWalls.end(), "no wall resolved to the high Image");
  auto const* data = found->wallMaskOverride.imageData();
  require(data != nullptr, "the winning mask lost its Image payload");
  require(data->resourceName == "mask/high.png",
          "the winning mask changed its resource");
  require(data->channel == 3, "the winning mask changed its channel");
  require(data->blendParameters == highImage.imageData()->blendParameters,
          "the winning mask changed its blend parameters");

  auto structural = highMapped;
  structural.contributesProperties = false;
  auto structuralWalls = wallsFor({low, structural});
  require(countMask(structuralWalls, image) == 1,
          "a property-transparent Primitive selected a wall mask value");
}

void overridesReachSplitBordersAndBothStepKinds() {
  auto image = maskImage("mask/split.png", 1, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f});
  auto base = square(0, 1);
  maskOnEdge(base, 2, image);  // top edge, split by the notch below.
  auto notch = square(1, 2);
  notch.contours[0] = rectangle(4 * U, 8 * U, 6 * U, 12 * U);
  auto splitWalls = wallsFor({base, notch});
  size_t mappedBorders = 0;
  for (auto const& wall : splitWalls) {
    mappedBorders += wall.kind == ArrangementWallKind::Border &&
                     wall.wallMaskOverride == image;
  }
  require(mappedBorders == 2,
          "a split source edge did not propagate its Image to every surviving sub-segment");

  PrimitivePropertySet lowProperties;
  lowProperties.floorZ = 0;
  lowProperties.ceilingZ = 48;
  PrimitivePropertySet highProperties;
  highProperties.floorZ = 8;
  highProperties.ceilingZ = 40;
  auto left = square(0, 1, lowProperties);
  left.contours[0] = rectangle(0, 0, 10 * U, 10 * U);
  maskOnEdge(left, 1, image);
  auto right = square(1, 2, highProperties);
  right.contours[0] = rectangle(10 * U, 0, 20 * U, 10 * U);
  auto steps = wallsFor({left, right});
  bool floor = false, ceiling = false;
  for (auto const& wall : steps) {
    if (wall.kind == ArrangementWallKind::FloorStep) {
      floor = wall.wallMaskOverride == image;
    }
    if (wall.kind == ArrangementWallKind::CeilingStep) {
      ceiling = wall.wallMaskOverride == image;
    }
  }
  require(floor && ceiling,
          "FloorStep and CeilingStep walls did not inherit their Arrangement edge's Image");
}

void erasedEdgesDoNotCreateWalls() {
  auto image = maskImage("mask/erased.png", 2, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f});
  auto left = square(0, 1);
  maskOnEdge(left, 1, image);
  auto right = square(1, 2);
  right.contours[0] = rectangle(10 * U, 0, 20 * U, 10 * U);
  auto walls = wallsFor({left, right});
  require(std::none_of(walls.begin(), walls.end(), [&](auto const& wall) {
            return wall.wallMaskOverride == image;
          }),
          "a mask was represented on an edge that the fold erased");
}
}  // namespace

int main() {
  try {
    triStateAndFoldPrecedenceAreExplicit();
    overridesReachSplitBordersAndBothStepKinds();
    erasedEdgesDoNotCreateWalls();
    std::cout << "Wall mask overrides resolve through the arrangement fold\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
