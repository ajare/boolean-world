#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
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

void authoritativeTreePreservesArbitraryDepth() {
  bw::core::MeshFilledRegion deepest{ring(-1, -1, 1, 1), {}};
  bw::core::MeshFilledRegion island{
      ring(-3, -3, 3, 3),
      {{ring(-2, -2, 2, 2), {deepest}}}};
  bw::core::MeshFilledRegion shell{
      ring(-5, -5, 5, 5),
      {{ring(-4, -4, 4, 4), {island}}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {shell}));

  auto const& stored = primitive->getShells();
  require(stored.size() == 1 && stored[0].holes.size() == 1 &&
              stored[0].holes[0].islands.size() == 1 &&
              stored[0].holes[0].islands[0].holes[0].islands.size() == 1,
          "the authoritative tree lost deep alternating containment");
  auto flattened = primitive->flattenTree();
  require(flattened.size() == 3 && flattened[0].size() == 2 &&
              flattened[1].size() == 2 && flattened[2].size() == 1,
          "pre-order flattening did not emit one filled region with direct Holes");
  for (auto const& polygon : flattened) {
    for (auto const& value : polygon) {
      float area = 0.0f;
      for (size_t i = 0; i < value.size(); ++i) {
        auto const& a = value[i].p;
        auto const& b = value[(i + 1) % value.size()].p;
        area += a.x * b.y - b.x * a.y;
      }
      require(area > 0.0f, "a stored Ring was not canonical anticlockwise");
    }
  }

  auto copy = std::unique_ptr<MeshPrimitive>(static_cast<MeshPrimitive*>(primitive->copy()));
  MeshPrimitive assigned(
      Primitive::Operation::Difference, Primitive::FillRule::NonZero, {});
  assigned = *primitive;
  auto rotated = std::unique_ptr<Primitive>(primitive->rotatedCopy(90.0f));
  require(copy->flattenTree().size() == 3 &&
              assigned.flattenTree().size() == 3 &&
              static_cast<MeshPrimitive*>(rotated.get())->flattenTree().size() == 3 &&
              copy->getNumVertices() == primitive->getNumVertices() &&
              assigned.getNumVertices() == primitive->getNumVertices() &&
              rotated->getBounds().getSize().x > 0.0f,
          "copying, assignment, rotation, bounds, or vertex counts lost tree data");

  auto proxy = primitive->createGeometryProxy();
  primitive->updateFromGeometryProxy(*proxy);
  require(primitive->flattenTree().size() == 3,
          "the temporary editing compatibility round-trip lost deep topology");
}

void deepTreeGeneratesAlternatingFilledRegions() {
  bw::core::MeshFilledRegion deepest{ring(-1, -1, 1, 1), {}};
  bw::core::MeshFilledRegion island{
      ring(-3, -3, 3, 3), {{ring(-2, -2, 2, 2), {deepest}}}};
  bw::core::MeshFilledRegion shell{
      ring(-5, -5, 5, 5), {{ring(-4, -4, 4, 4), {island}}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {shell}));
  primitive->updateVertexPositions();

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(std::vector<Primitive*>{primitive.get()});
  bw::core::ArrangementWorldData worldData(
      generator.getWorldData(), {{-10.0f, -10.0f}, {20.0f, 20.0f}},
      1.0f, 1.0f);
  require(worldData.getContainingFaceIndex({4.5f, 0.0f}) != ~0u,
          "the root Shell was not filled");
  require(worldData.getContainingFaceIndex({3.5f, 0.0f}) == ~0u,
          "the direct Hole was not empty");
  require(worldData.getContainingFaceIndex({2.5f, 0.0f}) != ~0u,
          "the Island was not filled");
  require(worldData.getContainingFaceIndex({1.5f, 0.0f}) == ~0u,
          "the nested Hole was not empty");
  require(worldData.getContainingFaceIndex({0.5f, 0.0f}) != ~0u,
          "the deeply nested Island was not filled");
}

void shallowConversionRejectsCrossEntryNestingAndMalformedTrees() {
  bool rejectedNesting = false;
  try {
    std::unique_ptr<MeshPrimitive> invalid(MeshPrimitive::fromComplexPolygons(
        Primitive::Operation::Union, Primitive::FillRule::EvenOdd,
        {{ring(-5, -5, 5, 5), ring(-4, -4, 4, 4)},
         {ring(-2, -2, 2, 2)}}));
  } catch (std::exception const&) {
    rejectedNesting = true;
  }
  require(rejectedNesting, "the shallow converter inferred cross-entry nesting");

  bool rejectedContainment = false;
  try {
    bw::core::MeshFilledRegion invalid{
        ring(-1, -1, 1, 1), {{ring(2, 2, 3, 3), {}}}};
    std::unique_ptr<MeshPrimitive> primitive(
        MeshPrimitive::fromTree(Primitive::Operation::Union, {invalid}));
  } catch (std::exception const&) {
    rejectedContainment = true;
  }
  require(rejectedContainment, "an uncontained Hole entered the authoritative tree");
}

}  // namespace

int main() {
  try {
    conversionsPreserveStorageOrderingAndDegenerateShapes();
    authoritativeTreePreservesArbitraryDepth();
    deepTreeGeneratesAlternatingFilledRegions();
    shallowConversionRejectsCrossEntryNestingAndMalformedTrees();
    std::cout << "MeshPrimitive geometry proxy tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
