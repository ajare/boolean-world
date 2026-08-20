#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/MeshPrimitive.h>

namespace {

using bw::core::ClosedPolygon;
using bw::core::ComplexPolygon;
using bw::core::MeshPrimitive;
using bw::core::Primitive;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ClosedPolygon ring(float minX, float minY, float maxX, float maxY) {
  return {{{minX, minY}}, {{maxX, minY}}, {{maxX, maxY}}, {{minX, maxY}}};
}

bool near(float a, float b) {
  return std::abs(a - b) < 1e-3f;
}

std::vector<ComplexPolygon> readProxy(wp::geometry::Mesh const& mesh) {
  std::vector<ComplexPolygon> result;
  for (auto polygonIndex = mesh.getFirstPolygonIndex();
       !mesh.polygonIndexIterationFinished(polygonIndex);
       polygonIndex = mesh.getNextPolygonIndex(polygonIndex)) {
    auto const& polygon = mesh.getPolygon(polygonIndex);
    if (polygon.isHole()) {
      continue;
    }
    ComplexPolygon complex;
    auto addRing = [&](uint32_t index) {
      ClosedPolygon value;
      for (auto vertex : mesh.getPolygon(index).getOrderedVertexIndices()) {
        value.emplace_back(mesh.getVertex(vertex).getPosition());
      }
      complex.push_back(std::move(value));
    };
    addRing(polygonIndex);
    for (auto hole : polygon.getHoleIndices()) {
      addRing(hole);
    }
    result.push_back(std::move(complex));
  }
  return result;
}

void requireEqual(
    std::vector<ComplexPolygon> const& expected,
    std::vector<ComplexPolygon> const& actual,
    std::string const& context) {
  require(expected.size() == actual.size(), context + ": ComplexPolygon count changed");
  for (size_t i = 0; i < expected.size(); ++i) {
    require(expected[i].size() == actual[i].size(), context + ": Ring count changed");
    for (size_t j = 0; j < expected[i].size(); ++j) {
      require(expected[i][j].size() == actual[i][j].size(), context + ": vertex count changed");
      auto const count = expected[i][j].size();
      size_t offset = count;
      for (size_t k = 0; k < count; ++k) {
        if (near(expected[i][j][0].p.x, actual[i][j][k].p.x) &&
            near(expected[i][j][0].p.y, actual[i][j][k].p.y)) {
          offset = k;
          break;
        }
      }
      require(offset != count, context + ": Ring " + std::to_string(i) + "/" +
                                   std::to_string(j) + " coordinates changed; expected first " +
                                   std::to_string(expected[i][j][0].p.x) + "," +
                                   std::to_string(expected[i][j][0].p.y) + " actual " +
                                   std::to_string(actual[i][j][0].p.x) + "," +
                                   std::to_string(actual[i][j][0].p.y));
      for (auto const& expectedVertex : expected[i][j]) {
        bool found = false;
        for (auto const& actualVertex : actual[i][j]) {
          found = found || (near(expectedVertex.p.x, actualVertex.p.x) &&
                            near(expectedVertex.p.y, actualVertex.p.y));
        }
        require(found, context + ": Ring " + std::to_string(i) + "/" +
                           std::to_string(j) + " coordinates changed");
      }
    }
  }
}

void conversionsPreserveStorageOrderingAndDegenerateShapes() {
  std::vector<std::vector<ComplexPolygon>> cases{
      {},
      {{ring(-1, -1, 1, 1)}},
      {{ring(-1, -1, 1, 1), ring(-0.5f, -0.5f, 0.5f, 0.5f)}},
      {{ring(-4, -1, -2, 1)}, {ring(2, -1, 4, 1)}}};

  for (size_t i = 0; i < cases.size(); ++i) {
    MeshPrimitive primitive(Primitive::Operation::Union, Primitive::FillRule::EvenOdd, cases[i]);
    primitive.setSize(20.0f, 30.0f);
    primitive.setPosition({17.0f, -9.0f});
    primitive.setOrientation(23.0f);
    auto proxy = primitive.createGeometryProxy();
    auto expected = readProxy(*proxy);
    primitive.updateFromGeometryProxy(*proxy);
    auto rebuilt = primitive.createGeometryProxy();
    requireEqual(expected, readProxy(*rebuilt),
                 "round-trip for degenerate case " + std::to_string(i));
  }
}

void islandContainmentIsDerived() {
  std::vector<ComplexPolygon> polygons{
      {ring(-5, -5, 5, 5), ring(-4, -4, 4, 4)},
      {ring(-2, -2, 2, 2)}};
  MeshPrimitive primitive(Primitive::Operation::Union, Primitive::FillRule::EvenOdd, polygons);
  auto proxy = primitive.createGeometryProxy();

  auto outer = proxy->getFirstPolygonIndex();
  require(proxy->getPolygon(outer).getHoleIndices().size() == 1,
          "Ring 0 did not own the remaining Ring as a hole");
  auto hole = proxy->getPolygon(outer).getHoleIndices().front();
  require(proxy->getPolygon(hole).isHole(),
          "the inner Ring was not represented as a hole");
  require(proxy->getContainingPolygon({0.0f, 0.0f}) != static_cast<int32_t>(outer),
          "filled island containment inside a hole was not re-derived");

  primitive.updateFromGeometryProxy(*proxy);
  auto roundTripped = primitive.createGeometryProxy();
  require(roundTripped->getNumPolygons() == 3,
          "island round-trip did not restore top-level island storage");
}

}  // namespace

int main() {
  try {
    conversionsPreserveStorageOrderingAndDegenerateShapes();
    islandContainmentIsDerived();
    std::cout << "MeshPrimitive geometry proxy tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
