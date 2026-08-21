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

bool sameRing(ClosedPolygon const& first, ClosedPolygon const& second) {
  if (first.size() != second.size() || first.empty()) return false;
  for (size_t offset = 0; offset < second.size(); ++offset) {
    bool forward = true;
    bool reverse = true;
    for (size_t i = 0; i < first.size(); ++i) {
      forward &= first[i].p == second[(offset + i) % second.size()].p;
      reverse &= first[i].p ==
                 second[(offset + second.size() - i) % second.size()].p;
    }
    if (forward || reverse) return true;
  }
  return false;
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
    auto proxy = primitive.createEditingProxy();
    auto expected = readProxy(proxy->getMesh());
    proxy->commitTo(primitive);
    auto rebuilt = primitive.createEditingProxy();
    requireEqual(expected, readProxy(rebuilt->getMesh()),
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

  auto proxy = primitive->createEditingProxy();
  auto mappings = proxy->getNodeMappings();
  require(mappings.size() == 5 &&
              mappings[0].role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Shell &&
              mappings[1].role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Hole &&
              mappings[2].role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Island &&
              mappings[3].role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Hole &&
              mappings[4].role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Island,
          "the proxy mappings lost arbitrary-depth structural roles");
  proxy->commitTo(*primitive);
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

void coincidentHoleAndIslandRemainIndependentAuthoredRings() {
  auto shared = ring(-2, -2, 2, 2);
  bw::core::MeshFilledRegion island{shared, {}};
  bw::core::MeshFilledRegion shell{
      ring(-4, -4, 4, 4), {{shared, {island}}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {shell}));
  auto proxy = primitive->createEditingProxy();
  require(proxy->getMesh().getNumVertices() == 8 &&
              proxy->getMesh().getNumEdges() == 8,
          "coincident Hole and Island boundaries were duplicated in the proxy");

  auto mappings = proxy->getNodeMappings();
  auto hole = mappings[1].polygonIndex;
  auto islandIndex = mappings[2].polygonIndex;
  require(proxy->getMesh().getPolygon(hole).getEdgeIndexSet() ==
              proxy->getMesh().getPolygon(islandIndex).getEdgeIndexSet(),
          "coincident Hole and Island Rings do not share Mesh edges");
  auto vertex = proxy->getMesh().getPolygon(hole).getOrderedVertexIndices().front();
  proxy->moveVertex(vertex, {0.25f, 0.0f});
  proxy->commitTo(*primitive);
  auto const& storedHole = primitive->getShells()[0].holes[0];
  require(storedHole.ring.size() == storedHole.islands[0].ring.size(),
          "coincident Rings did not remain independent authored values");
  for (auto const& holeVertex : storedHole.ring) {
    bool matched = false;
    for (auto const& islandVertex : storedHole.islands[0].ring)
      matched |= holeVertex.p == islandVertex.p;
    require(matched, "moving shared topology detached the authored boundary values");
  }
}

void hierarchyAwareProxyWeldsOnlyExactSharedTopology() {
  bw::core::MeshFilledRegion left{ring(-2, -1, 0, 1), {}};
  bw::core::MeshFilledRegion right{ring(0, -1, 2, 1), {}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {left, right}));
  auto proxy = primitive->createEditingProxy();
  auto const& mesh = proxy->getMesh();
  require(mesh.getNumVertices() == 6 && mesh.getNumEdges() == 7,
          "exactly shared Shell boundary topology was not welded");

  uint32_t sharedEdge = ~0u;
  for (auto edge = mesh.getFirstEdgeIndex();
       !mesh.edgeIndexIterationFinished(edge);
       edge = mesh.getNextEdgeIndex(edge)) {
    if (mesh.getEdge(edge).getPolygonReferences().size() == 2) sharedEdge = edge;
  }
  require(sharedEdge != ~0u, "the exact shared boundary has no shared Mesh edge");

  auto mappings = proxy->getNodeMappings();
  require(mappings.size() == 2 &&
              mappings[0].role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Shell &&
              mappings[1].role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Shell,
          "the proxy did not retain explicit Shell mappings");

  bw::core::MeshFilledRegion nearby{ring(0.00001f, -1, 2, 1), {}};
  auto nearPrimitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {left, nearby}));
  auto nearProxy = nearPrimitive->createEditingProxy();
  require(nearProxy->getMesh().getNumVertices() == 8,
          "near-but-not-equal coordinates were welded");
}

void sharedMutationsCommitToEveryAuthoredRingAtomically() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{ring(-2, -1, 0, 1), {}}, {ring(0, -1, 2, 1), {}}}));
  auto position = primitive->getPosition();
  auto size = primitive->getSize();
  auto orientation = primitive->getOrientation();
  auto proxy = primitive->createEditingProxy();

  uint32_t sharedEdge = ~0u;
  for (auto edge = proxy->getMesh().getFirstEdgeIndex();
       !proxy->getMesh().edgeIndexIterationFinished(edge);
       edge = proxy->getMesh().getNextEdgeIndex(edge)) {
    if (proxy->getMesh().getEdge(edge).getPolygonReferences().size() == 2) sharedEdge = edge;
  }
  require(sharedEdge != ~0u, "shared mutation fixture has no welded edge");
  wp::geometry::SplitEdgeResult split;
  require(proxy->splitEdge(sharedEdge, &split), "the shared edge was not split");
  proxy->commitTo(*primitive);
  require(primitive->getShells()[0].ring.size() == 5 &&
              primitive->getShells()[1].ring.size() == 5,
          "splitting shared topology detached one authored Ring");
  require(primitive->getPosition() == position && primitive->getSize() == size &&
              primitive->getOrientation() == orientation,
          "the rest-pose round trip changed the Primitive transform");

  auto deleteProxy = primitive->createEditingProxy();
  uint32_t sharedVertex = ~0u;
  for (auto vertex = deleteProxy->getMesh().getFirstVertexIndex();
       !deleteProxy->getMesh().vertexIndexIterationFinished(vertex);
       vertex = deleteProxy->getMesh().getNextVertexIndex(vertex)) {
    size_t participatingRings = 0;
    for (auto edge : deleteProxy->getMesh().getVertex(vertex).getEdgeReferences()) {
      participatingRings += deleteProxy->getMesh().getEdge(edge).getPolygonReferences().size();
    }
    if (participatingRings > 2) {
      sharedVertex = vertex;
      break;
    }
  }
  require(sharedVertex != ~0u && deleteProxy->removeVertex(sharedVertex),
          "deleting a shared Vertex was refused");
  deleteProxy->commitTo(*primitive);
  require(primitive->getShells()[0].ring.size() == 4 &&
              primitive->getShells()[1].ring.size() == 4,
          "deleting a shared Vertex did not update every participating Ring");
}

void fillHoleWrapsImmediateIslandsWithoutLosingDescendants() {
  bw::core::MeshFilledRegion descendant{ring(-6, -1, -5, 1), {}};
  bw::core::MeshFilledRegion left{
      ring(-7, -3, -4, 3), {{ring(-6.5f, -2, -4.5f, 2), {descendant}}}};
  bw::core::MeshFilledRegion right{ring(4, -3, 7, 3), {}};
  bw::core::MeshFilledRegion shell{
      ring(-10, -10, 10, 10), {{ring(-9, -9, 9, 9), {left, right}}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {shell}));
  auto proxy = primitive->createEditingProxy();
  auto mappings = proxy->getNodeMappings();
  auto originalHole = mappings[1].polygonIndex;
  auto filled = proxy->fillHole(originalHole);
  require(filled != ~0u, "a Hole containing Islands could not be filled");
  proxy->commitTo(*primitive);

  auto const& retainedHole = primitive->getShells()[0].holes[0];
  require(retainedHole.islands.size() == 1,
          "Fill Hole did not create exactly one direct Island");
  auto const& wrapper = retainedHole.islands[0];
  require(wrapper.holes.size() == 2 &&
              wrapper.holes[0].islands.size() == 1 &&
              wrapper.holes[1].islands.size() == 1 &&
              wrapper.holes[0].islands[0].holes.size() == 1 &&
              wrapper.holes[0].islands[0].holes[0].islands.size() == 1,
          "Fill Hole did not wrap every immediate Island with descendants intact");
  require(sameRing(retainedHole.ring, wrapper.ring) &&
              sameRing(wrapper.holes[0].ring,
                       wrapper.holes[0].islands[0].ring) &&
              sameRing(wrapper.holes[1].ring,
                       wrapper.holes[1].islands[0].ring),
          "Fill Hole did not retain independent coincident Ring values");

  auto rebuilt = primitive->createEditingProxy();
  auto rebuiltMappings = rebuilt->getNodeMappings();
  size_t weldedPairs = 0;
  auto readRing = [&](uint32_t polygonIndex) {
    ClosedPolygon result;
    for (auto vertex : rebuilt->getPolygon(polygonIndex).getOrderedVertexIndices()) {
      result.emplace_back(rebuilt->getVertex(vertex).getPosition());
    }
    return result;
  };
  for (auto const& mapping : rebuiltMappings) {
    if (mapping.role != bw::core::MeshPrimitiveEditingProxy::NodeRole::Hole) {
      continue;
    }
    auto const& hole = rebuilt->getPolygon(mapping.polygonIndex);
    for (auto const& candidate : rebuiltMappings) {
      if (candidate.role == bw::core::MeshPrimitiveEditingProxy::NodeRole::Island &&
          candidate.parentPolygonIndex == mapping.polygonIndex &&
          sameRing(readRing(mapping.polygonIndex), readRing(candidate.polygonIndex))) {
        ++weldedPairs;
        require(hole.getEdgeIndexSet() ==
                    rebuilt->getPolygon(candidate.polygonIndex).getEdgeIndexSet(),
                "reactivation did not reconstruct a welded Hole/Island boundary");
      }
    }
  }
  require(weldedPairs == 3,
          "reactivation did not retain all three created coincident boundaries");
}

void failedProxyCommitLeavesAuthoredAndDerivedGeometryUnchanged() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{{ring(-2, -2, 2, 2), {}}}}));
  auto beforeTree = primitive->flattenTree();
  auto beforeVertices = primitive->getVertices();
  auto proxy = primitive->createEditingProxy();
  auto first = proxy->getMesh().getFirstVertexIndex();
  auto second = proxy->getMesh().getNextVertexIndex(first);
  proxy->moveVertex(first,
                    proxy->getMesh().getVertex(second).getPosition() -
                        proxy->getMesh().getVertex(first).getPosition());
  bool rejected = false;
  try {
    proxy->commitTo(*primitive);
  } catch (std::exception const&) {
    rejected = true;
  }
  require(rejected, "an invalid complete proxy candidate committed");
  requireEqual(beforeTree, primitive->flattenTree(),
               "failed commit changed the authoritative tree");
  requireEqual(beforeVertices, primitive->getVertices(),
               "failed commit changed derived geometry");
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
    coincidentHoleAndIslandRemainIndependentAuthoredRings();
    hierarchyAwareProxyWeldsOnlyExactSharedTopology();
    sharedMutationsCommitToEveryAuthoredRingAtomically();
    fillHoleWrapsImmediateIslandsWithoutLosingDescendants();
    failedProxyCommitLeavesAuthoredAndDerivedGeometryUnchanged();
    shallowConversionRejectsCrossEntryNestingAndMalformedTrees();
    std::cout << "MeshPrimitive geometry proxy tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
