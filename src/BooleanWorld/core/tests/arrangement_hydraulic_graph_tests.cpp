#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/Elevation.h>

namespace {
using bw::core::ArrangementWorldData;
using bw::core::Elevation;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementResult;
using bw::core::arr::Contour;
using bw::core::arr::HydraulicCell;
using bw::core::arr::HydraulicLink;
using bw::core::arr::ToFixedPointCoordinate;

constexpr double Epsilon = 0.001;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void requireNear(double actual, double expected, std::string const& message) {
  if (std::abs(actual - expected) >= Epsilon) {
    throw std::runtime_error(
        message + " (expected " + std::to_string(expected) + ", got " +
        std::to_string(actual) + ")");
  }
}

Contour contour(std::initializer_list<std::array<double, 2>> vertices) {
  Contour result;
  for (auto const& vertex : vertices) {
    result.push_back(
        {ToFixedPointCoordinate(vertex[0]),
         ToFixedPointCoordinate(vertex[1])});
  }
  return result;
}

ArrangementPrimitive primitive(
    Contour shape, uint32_t primitiveIndex, Elevation floor, Elevation ceiling,
    float liquidLevel, double rawArea) {
  PrimitivePropertySet properties;
  properties.floorZ = floor;
  properties.ceilingZ = ceiling;
  properties.liquidLevel = liquidLevel;
  ArrangementPrimitive result{
      {std::move(shape)}, Primitive::Operation::Union, Primitive::FillRule::NonZero, 0, primitiveIndex, properties};
  result.rawArea = rawArea;
  return result;
}

ArrangementPrimitive rectangle(
    double minX, double minY, double maxX, double maxY,
    uint32_t primitiveIndex, Elevation floor, Elevation ceiling,
    float liquidLevel) {
  return primitive(
      contour({{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}}),
      primitiveIndex, floor, ceiling, liquidLevel,
      (maxX - minX) * (maxY - minY));
}

bool pointInCell(wp::Vector2 const& point, HydraulicCell const& cell) {
  auto cross = [](wp::Vector2 const& a, wp::Vector2 const& b,
                  wp::Vector2 const& p) {
    return (b.x - a.x) * (p.y - a.y) -
           (b.y - a.y) * (p.x - a.x);
  };
  auto side0 = cross(cell.positions[0], cell.positions[1], point);
  auto side1 = cross(cell.positions[1], cell.positions[2], point);
  auto side2 = cross(cell.positions[2], cell.positions[0], point);
  return (side0 >= 0.0f && side1 >= 0.0f && side2 >= 0.0f) ||
         (side0 <= 0.0f && side1 <= 0.0f && side2 <= 0.0f);
}

uint32_t containingCell(
    std::vector<HydraulicCell> const& cells, wp::Vector2 const& point,
    uint32_t face) {
  auto found = std::ranges::find_if(cells, [&](auto const& cell) {
    return cell.triangle.face == face && pointInCell(point, cell);
  });
  if (found == cells.end()) {
    throw std::runtime_error("no Hydraulic cell contained the test point");
  }
  return uint32_t(std::distance(cells.begin(), found));
}

double liquidDepthAt(
    ArrangementWorldData const& data, wp::Vector2 const& point,
    uint32_t face) {
  auto const& cells = data.getHydraulicCells();
  auto cell = containingCell(cells, point, face);
  auto elevation = data.getLiquidPoolElevations()[cell];
  if (!std::isfinite(elevation)) return 0.0;
  auto floor = cells[cell].floor.evaluate(point);
  auto ceiling = cells[cell].ceiling.evaluate(point);
  return std::clamp(
      elevation - floor, 0.0,
      double(std::max(0.0f, ceiling - floor)));
}

uint32_t primitiveFace(
    ArrangementResult const& arrangement, uint32_t primitiveIndex) {
  for (uint32_t face = 1; face < uint32_t(arrangement.faces.size()); ++face) {
    if (arrangement.faces[face].solid &&
        arrangement.faces[face].primitiveIndex == primitiveIndex) {
      return face;
    }
  }
  throw std::runtime_error("no face was owned by the requested Primitive");
}

bool connectedBelow(
    uint32_t cell0, uint32_t cell1, double elevation,
    std::vector<HydraulicLink> const& links, size_t cellCount) {
  std::vector<uint32_t> parent(cellCount);
  for (uint32_t cell = 0; cell < cellCount; ++cell) parent[cell] = cell;
  auto root = [&](uint32_t cell) {
    while (parent[cell] != cell) cell = parent[cell];
    return cell;
  };
  for (auto const& link : links) {
    if (link.drain || link.sill > elevation) continue;
    auto first = root(link.cell0);
    auto second = root(link.cell1);
    parent[second] = first;
  }
  return root(cell0) == root(cell1);
}

void aSharedEdgesSillUsesItsLowestTraversableOpening() {
  // Along the shared edge x=0, y=0..10, the left floor is y. The right
  // ceiling is 2y-5 while its own floor is -5. Thus max(floors)=y and
  // min(ceilings)=2y-5: the opening exists only above y=5 and its Sill is 5.
  auto arrangement = bw::core::arr::BuildArrangement({
      rectangle(-10, 0, 0, 10, 0, Elevation{0.0f, {0.0f, 1.0f}},
                Elevation{20.0f}, 0.0f),
      rectangle(0, 0, 10, 10, 1, Elevation{-5.0f},
                Elevation{-5.0f, {0.0f, 2.0f}}, 0.0f),
  });
  auto triangles = bw::core::arr::BuildArrangementTriangles(*arrangement);
  auto cells = bw::core::arr::BuildHydraulicCells(*arrangement, triangles);
  auto links = bw::core::arr::BuildHydraulicLinks(*arrangement, cells);
  auto leftFace = primitiveFace(*arrangement, 0);
  auto rightFace = primitiveFace(*arrangement, 1);

  auto shared = std::ranges::find_if(links, [&](auto const& link) {
    return !link.drain &&
           ((cells[link.cell0].triangle.face == leftFace &&
             cells[link.cell1].triangle.face == rightFace) ||
            (cells[link.cell0].triangle.face == rightFace &&
             cells[link.cell1].triangle.face == leftFace));
  });
  require(shared != links.end(),
          "the traversable part of a shared sloped edge produced no hydraulic link");
  requireNear(shared->sill, 5.0,
              "the shared edge Sill did not use the lowest traversable opening");

  auto sealed = bw::core::arr::BuildArrangement({
      rectangle(-10, 0, 0, 10, 0, Elevation{0.0f, {0.0f, 1.0f}},
                Elevation{20.0f}, 0.0f),
      rectangle(0, 0, 10, 10, 1, Elevation{-5.0f}, Elevation{-5.0f},
                0.0f),
  });
  auto sealedTriangles = bw::core::arr::BuildArrangementTriangles(*sealed);
  auto sealedCells =
      bw::core::arr::BuildHydraulicCells(*sealed, sealedTriangles);
  auto sealedLinks =
      bw::core::arr::BuildHydraulicLinks(*sealed, sealedCells);
  auto sealedLeft = primitiveFace(*sealed, 0);
  auto sealedRight = primitiveFace(*sealed, 1);
  require(std::ranges::none_of(sealedLinks, [&](auto const& link) {
            if (link.drain) return false;
            auto face0 = sealedCells[link.cell0].triangle.face;
            auto face1 = sealedCells[link.cell1].triangle.face;
            return (face0 == sealedLeft && face1 == sealedRight) ||
                   (face0 == sealedRight && face1 == sealedLeft);
          }),
          "a shared edge with no positive-clearance portion became hydraulically connected");
}

std::vector<ArrangementPrimitive> uBasin(float sourceLiquidLevel) {
  // One non-convex face has two low legs connected only by its y=8..10 crown.
  // Its floor elevation is y, so sub-Sill Liquid entering the left leg cannot
  // reach the equally low right leg through the artificial triangulation.
  return {
      rectangle(-10, 0, 0, 2, 0, Elevation{0.0f}, Elevation{20.0f},
                sourceLiquidLevel),
      primitive(
          contour({{0, 0}, {2, 0}, {2, 8}, {8, 8}, {8, 0}, {10, 0}, {10, 10}, {0, 10}}),
          1, Elevation{0.0f, {0.0f, 1.0f}}, Elevation{20.0f}, 0.0f,
          52.0),
  };
}

std::shared_ptr<ArrangementWorldData const> uBasinData(float liquidLevel) {
  auto arrangement = bw::core::arr::BuildArrangement(uBasin(liquidLevel));
  return std::make_shared<ArrangementWorldData>(
      arrangement, wp::BoundingBox({-20.0f, -5.0f}, {20.0f, 15.0f}),
      8.0f);
}

void artificialEdgesKeepNonConvexLowRegionsSeparateUntilTheirSill() {
  auto data = uBasinData(10.0f);  // 200 volume: less than 224 below y=8.
  auto const& arrangement = data->getArrangement();
  auto const& cells = data->getHydraulicCells();
  auto links = bw::core::arr::BuildHydraulicLinks(arrangement, cells);
  auto face = primitiveFace(arrangement, 1);
  auto left = containingCell(cells, {0.5f, 0.5f}, face);
  auto right = containingCell(cells, {9.0f, 1.0f}, face);

  require(!connectedBelow(left, right, 7.999, links, cells.size()),
          "artificial triangulation connected separate low regions below their route");
  require(connectedBelow(left, right, 8.0, links, cells.size()),
          "artificial triangulation did not link low regions when their route was reached");
  require(liquidDepthAt(*data, {0.5f, 0.5f}, face) > 0.0,
          "Liquid entering the non-convex face did not wet its reached low region");
  requireNear(liquidDepthAt(*data, {9.0f, 1.0f}, face), 0.0,
              "an unreached low region in one non-convex face became wet early");

  double settledVolume = 0.0;
  auto const& elevations = data->getLiquidPoolElevations();
  for (size_t cell = 0; cell < cells.size(); ++cell) {
    if (std::isfinite(elevations[cell])) {
      settledVolume += cells[cell].volumeBelow(elevations[cell]);
    }
  }
  requireNear(settledVolume, 200.0,
              "the Hydraulic cell graph did not conserve authored volume");

  auto merged = uBasinData(15.0f);  // 300 volume rises above the y=8 route.
  auto mergedFace = primitiveFace(merged->getArrangement(), 1);
  require(liquidDepthAt(*merged, {9.0f, 1.0f}, mergedFace) > 0.0,
          "the second low region stayed dry after the connecting Sill was reached");

  auto repeated = uBasinData(10.0f);
  require(repeated->getLiquidPoolElevations() ==
              data->getLiquidPoolElevations(),
          "rebuilding the same Hydraulic cell graph changed Pool elevations");
}

std::shared_ptr<ArrangementWorldData const> slopedDrain(
    float liquidLevel, bool openBoundary) {
  auto basin = rectangle(
      0, 0, 10, 10, 0, Elevation{0.0f, {1.0f, 0.0f}},
      Elevation{20.0f}, liquidLevel);
  if (openBoundary) {
    // Contour edges are bottom, right, top, left. Only the high x=10 edge is
    // explicitly non-colliding; every ordinary Border remains watertight.
    basin.contourEdgeOverrides = {{std::nullopt, false}};
  }
  auto arrangement = bw::core::arr::BuildArrangement({basin});
  return std::make_shared<ArrangementWorldData>(
      arrangement, wp::BoundingBox({-5.0f, -5.0f}, {15.0f, 15.0f}),
      8.0f);
}

void onlyAReachedExplicitOpeningDrainsASlopedPool() {
  auto below = slopedDrain(2.0f, true);
  auto belowFace = primitiveFace(below->getArrangement(), 0);
  require(liquidDepthAt(*below, {1.0f, 5.0f}, belowFace) > 0.0,
          "a sloped Pool drained before reaching its high open boundary");

  auto reached = slopedDrain(6.0f, true);
  auto reachedFace = primitiveFace(reached->getArrangement(), 0);
  requireNear(liquidDepthAt(*reached, {1.0f, 5.0f}, reachedFace), 0.0,
              "a Pool that reached an explicitly open boundary did not drain");

  auto colliding = slopedDrain(6.0f, false);
  auto collidingFace = primitiveFace(colliding->getArrangement(), 0);
  require(liquidDepthAt(*colliding, {1.0f, 5.0f}, collidingFace) > 0.0,
          "an ordinary colliding Border drained a sloped Pool");
}
}  // namespace

int main() {
  try {
    aSharedEdgesSillUsesItsLowestTraversableOpening();
    artificialEdgesKeepNonConvexLowRegionsSeparateUntilTheirSill();
    onlyAReachedExplicitOpeningDrainsASlopedPool();
    std::cout << "Sloped Hydraulic cell links spill, merge, and drain deterministically\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
