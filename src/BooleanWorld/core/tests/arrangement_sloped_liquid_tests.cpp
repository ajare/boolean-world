#include <array>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <memory>
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
using bw::core::arr::Contour;
using bw::core::arr::HydraulicCell;
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

ArrangementPrimitive basin(Contour shape, float liquidLevel = 2.0f) {
  PrimitivePropertySet properties;
  properties.floorZ = Elevation{0.0f, {1.0f, 0.0f}};
  properties.ceilingZ = 20.0f;
  properties.liquidLevel = liquidLevel;
  ArrangementPrimitive result{
      {std::move(shape)}, Primitive::Operation::Union, Primitive::FillRule::NonZero, 0, 0, properties};
  result.rawArea = 100.0;
  return result;
}

std::shared_ptr<ArrangementWorldData const> worldData(
    Contour shape, float liquidLevel = 2.0f) {
  auto arrangement = bw::core::arr::BuildArrangement(
      {basin(std::move(shape), liquidLevel)});
  return std::make_shared<ArrangementWorldData>(
      arrangement, wp::BoundingBox({-20.0f, -20.0f}, {50.0f, 50.0f}),
      8.0f);
}

void aHydraulicCellIntegratesItsAffineColumn() {
  HydraulicCell cell;
  cell.positions = {{{0.0f, 0.0f}, {10.0f, 0.0f}, {0.0f, 10.0f}}};
  cell.worldArea = 50.0;
  cell.floor = Elevation{0.0f, {1.0f, 0.0f}};
  cell.ceiling = Elevation{12.0f, {0.5f, 0.0f}};

  requireNear(cell.volumeBelow(-1.0), 0.0,
              "a Hydraulic cell held volume below its lowest floor");
  requireNear(cell.volumeBelow(5.0), 625.0 / 6.0,
              "a partly wet affine wedge integrated the wrong volume");
  requireNear(cell.volumeBelow(25.0), 1550.0 / 3.0,
              "a Hydraulic cell did not integrate its affine full capacity");
  requireNear(cell.capacityBelow(100.0), cell.volumeBelow(25.0),
              "capacity above the ceiling changed after the cell was full");
}

void arrangementTrianglesBecomeHydraulicCells() {
  auto arrangement = bw::core::arr::BuildArrangement(
      {basin(contour({{0, 0}, {10, 0}, {10, 10}, {0, 10}}))});
  auto triangles = bw::core::arr::BuildArrangementTriangles(*arrangement);
  auto cells = bw::core::arr::BuildHydraulicCells(*arrangement, triangles);

  require(cells.size() == triangles.size() && cells.size() == 2,
          "Arrangement triangles did not become one Hydraulic cell each");
  for (size_t index = 0; index < cells.size(); ++index) {
    require(cells[index].triangle.face == triangles[index].face,
            "a Hydraulic cell lost its Arrangement face");
    for (size_t corner = 0; corner < 3; ++corner) {
      requireNear(
          cells[index].triangle.floor.elevation[corner],
          cells[index].positions[corner].x,
          "a Hydraulic cell did not retain its affine floor");
      requireNear(cells[index].triangle.ceiling.elevation[corner], 20.0,
                  "a Hydraulic cell did not retain its affine ceiling");
    }
  }
}

void oneSlopedBasinSettlesToAHorizontalShoreline() {
  auto data = worldData(contour({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
  auto const expectedElevation = std::sqrt(40.0);

  auto lowDepth = data->getLiquidDepth({1.0f, 5.0f});
  auto middleDepth = data->getLiquidDepth({5.0f, 5.0f});
  requireNear(lowDepth, expectedElevation - 1.0,
              "Liquid depth did not follow the local sloped floor");
  requireNear(middleDepth, expectedElevation - 5.0,
              "Liquid depth was constant beneath a horizontal Pool");
  requireNear(data->getLiquidDepth({8.0f, 5.0f}), 0.0,
              "the dry side of the shoreline reported Liquid depth");
  requireNear(data->getLiquidSurfaceHeight({1.0f, 5.0f}), expectedElevation,
              "the low side reported the wrong Pool elevation");
  requireNear(data->getLiquidSurfaceHeight({5.0f, 5.0f}), expectedElevation,
              "one Pool did not retain one horizontal elevation");
  require(!std::isfinite(data->getLiquidSurfaceHeight({8.0f, 5.0f})),
          "the dry side of the shoreline reported a Liquid surface");

  double projectedArea = 0.0;
  bool foundShoreline = false;
  for (auto const& triangle : data->getLiquidSurfaceTriangles()) {
    requireNear(triangle.elevation, expectedElevation,
                "visible Liquid geometry was not horizontal");
    auto const ab = triangle.positions[1] - triangle.positions[0];
    auto const ac = triangle.positions[2] - triangle.positions[0];
    projectedArea +=
        std::abs(double(ab.x) * ac.y - double(ab.y) * ac.x) * 0.5;
    for (auto const& position : triangle.positions) {
      require(position.x <= expectedElevation + Epsilon,
              "visible Liquid crossed onto the dry slope");
      foundShoreline |=
          std::abs(position.x - expectedElevation) < Epsilon;
    }
  }
  require(foundShoreline,
          "visible Liquid geometry did not contain the expected shoreline");
  requireNear(projectedArea, expectedElevation * 10.0,
              "visible Liquid geometry covered the wrong wet area");

  double settledVolume = 0.0;
  auto const& cells = data->getHydraulicCells();
  auto const& elevations = data->getLiquidPoolElevations();
  for (size_t index = 0; index < cells.size(); ++index) {
    settledVolume += cells[index].volumeBelow(elevations[index]);
  }
  requireNear(settledVolume, 200.0,
              "the nonlinear Pool solve did not conserve authored volume");

  auto repeated = worldData(
      contour({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
  require(repeated->getLiquidPoolElevations() ==
              data->getLiquidPoolElevations(),
          "repeated sloped Pool solves were not deterministic");
}

void aCompletelyFloodedCellDrawsNoFreeSurface() {
  auto data = worldData(
      contour({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 100.0f);
  require(data->getLiquidSurfaceTriangles().empty(),
          "a basin flooded to its ceiling drew an impossible free surface");
  requireNear(data->getLiquidDepth({5.0f, 5.0f}), 15.0,
              "a flooded cell did not report its full local depth");
}

void subdividingTheSameBasinChangesNeitherVolumeNorEquilibrium() {
  auto ordinary = worldData(
      contour({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
  auto subdivided = worldData(
      contour({{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}}));

  require(subdivided->getHydraulicCells().size() >
              ordinary->getHydraulicCells().size(),
          "the comparison fixture did not subdivide the basin");
  requireNear(
      subdivided->getLiquidSurfaceHeight({1.0f, 5.0f}),
      ordinary->getLiquidSurfaceHeight({1.0f, 5.0f}),
      "subdivision changed the Pool equilibrium elevation");

  auto seededVolume = [](ArrangementWorldData const& data) {
    auto const& arrangement = data.getArrangement();
    auto depths =
        bw::core::arr::ComputeUndistributedLiquidDepths(arrangement);
    double volume = 0.0;
    for (size_t faceIndex = 1; faceIndex < arrangement.faces.size();
         ++faceIndex) {
      volume += depths[faceIndex] * bw::core::arr::FaceArea(
                                        arrangement.faces[faceIndex],
                                        arrangement);
    }
    return volume;
  };
  auto settledVolume = [](ArrangementWorldData const& data) {
    double volume = 0.0;
    auto const& cells = data.getHydraulicCells();
    auto const& elevations = data.getLiquidPoolElevations();
    for (size_t index = 0; index < cells.size(); ++index) {
      volume += cells[index].volumeBelow(elevations[index]);
    }
    return volume;
  };
  requireNear(seededVolume(*ordinary), 200.0,
              "the original basin seeded the wrong authored volume");
  requireNear(seededVolume(*subdivided), 200.0,
              "subdividing the basin changed its seeded volume");
  requireNear(settledVolume(*ordinary), 200.0,
              "the original basin lost authored volume at equilibrium");
  requireNear(settledVolume(*subdivided), 200.0,
              "subdividing the basin changed equilibrium volume");
}
}  // namespace

int main() {
  try {
    aHydraulicCellIntegratesItsAffineColumn();
    arrangementTrianglesBecomeHydraulicCells();
    oneSlopedBasinSettlesToAHorizontalShoreline();
    aCompletelyFloodedCellDrawsNoFreeSurface();
    subdividingTheSameBasinChangesNeitherVolumeNorEquilibrium();
    std::cout << "Sloped Hydraulic cells conserve and render one horizontal Pool\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
