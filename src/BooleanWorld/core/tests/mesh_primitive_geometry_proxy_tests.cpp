#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/MeshPrimitive.h>

namespace {

using bw::core::ClosedPolygon;
using bw::core::ComplexPolygon;
using bw::core::MeshPrimitive;
using bw::core::MeshPrimitiveEditingProxy;
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
    auto primitive = std::unique_ptr<MeshPrimitive>(
        MeshPrimitive::fromComplexPolygons(Primitive::Operation::Union, cases[i]));
    primitive->setSize(20.0f, 30.0f);
    primitive->setPosition({17.0f, -9.0f});
    primitive->setOrientation(23.0f);
    auto proxy = primitive->createEditingProxy();
    auto expected = readProxy(proxy->getMesh());
    proxy->commitTo(*primitive);
    auto rebuilt = primitive->createEditingProxy();
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
  auto assigned = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Difference, {}));
  *assigned = *primitive;
  auto rotated = std::unique_ptr<Primitive>(primitive->rotatedCopy(90.0f));
  require(copy->flattenTree().size() == 3 &&
              assigned->flattenTree().size() == 3 &&
              static_cast<MeshPrimitive*>(rotated.get())->flattenTree().size() == 3 &&
              copy->getNumVertices() == primitive->getNumVertices() &&
              assigned->getNumVertices() == primitive->getNumVertices() &&
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
      1.0f);
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

void removingTwoSidedEdgeMergesSiblingRings() {
  bw::core::MeshFilledRegion left{ring(-4, -3, 0, 3), {}};
  bw::core::MeshFilledRegion right{ring(0, -3, 4, 3), {}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {left, right}));
  auto proxy = primitive->createEditingProxy();
  uint32_t sharedEdge = ~0u;
  for (auto edge = proxy->getFirstEdgeIndex();
       !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge)) {
    if (proxy->getEdge(edge).getPolygonReferences().size() == 2) {
      sharedEdge = edge;
      break;
    }
  }
  require(sharedEdge != ~0u && proxy->removeEdge(sharedEdge),
          "a two-sided Edge between sibling Shells was not removed");
  proxy->commitTo(*primitive);
  require(primitive->getShells().size() == 1 &&
              primitive->getShells()[0].ring.size() == 6,
          "removing a two-sided Edge did not merge its two open Shells");

  auto shared = ring(-2, -2, 2, 2);
  bw::core::MeshFilledRegion island{shared, {}};
  bw::core::MeshFilledRegion shell{
      ring(-5, -5, 5, 5), {{shared, {island}}}};
  auto nestedPrimitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {shell}));
  auto nested = nestedPrimitive->createEditingProxy();
  auto mappings = nested->getNodeMappings();
  auto hole = mappings[1].polygonIndex;
  auto nestedSharedEdge = *nested->getPolygon(hole).getEdgeIndexSet().begin();
  require(!nested->removeEdge(nestedSharedEdge),
          "a two-sided Edge between a Hole and its Island was merged");
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

void sliceDividesFilledRingsAndRetainsHoles() {
  ClosedPolygon shell{
      {{-10, -10}}, {{0, -10}}, {{10, -10}}, {{10, 10}}, {{0, 10}}, {{-10, 10}}};
  bw::core::MeshFilledRegion source{
      shell, {{ring(-8, -4, -2, 4), {}}, {ring(2, -4, 8, 4), {}}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {source}));
  auto proxy = primitive->createEditingProxy();
  auto ordered = proxy->getPolygon(proxy->getFirstPolygonIndex())
                     .getOrderedVertexIndices();
  auto centreY = 0.0f;
  for (auto index : ordered) centreY += proxy->getVertex(index).getPosition().y;
  centreY /= static_cast<float>(ordered.size());
  auto bottom = *std::find_if(ordered.begin(), ordered.end(), [&](uint32_t index) {
    auto const& position = proxy->getVertex(index).getPosition();
    return near(position.x, 0.0f) && position.y < centreY;
  });
  auto top = *std::find_if(ordered.begin(), ordered.end(), [&](uint32_t index) {
    auto const& position = proxy->getVertex(index).getPosition();
    return near(position.x, 0.0f) && position.y > centreY;
  });
  require(proxy->sliceFilledRing(proxy->getFirstPolygonIndex(), bottom, top),
          "an unobstructed Shell chord was not sliced");
  proxy->commitTo(*primitive);
  require(primitive->getShells().size() == 2 &&
              primitive->getShells()[0].holes.size() == 1 &&
              primitive->getShells()[1].holes.size() == 1,
          "Slice did not create two Shells or retain each direct Hole");

  auto refused = primitive->createEditingProxy();
  auto firstRing = refused->getNodeMappings().front().polygonIndex;
  auto adjacent = refused->getPolygon(firstRing).getOrderedVertexIndices();
  require(!refused->sliceFilledRing(firstRing, adjacent[0], adjacent[1]),
          "Slice accepted adjacent Vertices");

  ClosedPolygon crossingShell{
      {{-10, -10}}, {{10, -10}}, {{10, 0}}, {{10, 10}}, {{-10, 10}}, {{-10, 0}}};
  auto crossingPrimitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(
          Primitive::Operation::Union,
          {{crossingShell, {{ring(-2, -2, 2, 2), {}}}}}));
  auto crossing = crossingPrimitive->createEditingProxy();
  auto crossingOuter = crossing->getNodeMappings().front().polygonIndex;
  auto crossingVertices = crossing->getPolygon(crossingOuter).getOrderedVertexIndices();
  auto crossingCentreY = 0.0f;
  for (auto index : crossingVertices) {
    crossingCentreY += crossing->getVertex(index).getPosition().y;
  }
  crossingCentreY /= static_cast<float>(crossingVertices.size());
  auto left = std::find_if(crossingVertices.begin(), crossingVertices.end(), [&](uint32_t index) {
    auto const& position = crossing->getVertex(index).getPosition();
    return position.x < 0.0f && near(position.y, crossingCentreY);
  });
  auto right = std::find_if(crossingVertices.begin(), crossingVertices.end(), [&](uint32_t index) {
    auto const& position = crossing->getVertex(index).getPosition();
    return position.x > 0.0f && near(position.y, crossingCentreY);
  });
  require(left != crossingVertices.end() && right != crossingVertices.end() &&
              !crossing->sliceFilledRing(crossingOuter, *left, *right),
          "Slice crossed a Hole boundary");
  auto crossingMappings = crossing->getNodeMappings();
  auto crossingHole = std::find_if(
      crossingMappings.begin(), crossingMappings.end(), [](auto const& item) {
        return item.role ==
               bw::core::MeshPrimitiveEditingProxy::NodeRole::Hole;
      });
  require(crossingHole != crossingMappings.end(),
          "the crossing fixture had no Hole");
  auto holeOnlyVertex =
      crossing->getPolygon(crossingHole->polygonIndex)
          .getOrderedVertexIndices()
          .front();
  require(!crossing->sliceFilledRing(
              crossingOuter, *left, holeOnlyVertex),
          "Slice accepted an endpoint owned only by another Ring");

  // A filled child may share just one proxy Vertex with its containing Hole.
  // The parent's incident Edges touch the chord endpoint but do not obstruct
  // slicing the Island: both authored vertices at that position remain a
  // valid selection within the same containment family.
  ClosedPolygon touchingIsland{
      {{-8, -8}}, {{0, -6}}, {{0, 0}}, {{-6, 0}}};
  bw::core::MeshFilledRegion island{touchingIsland, {}};
  bw::core::MeshFilledRegion containingShell{
      ring(-10, -10, 10, 10), {{ring(-8, -8, 8, 8), {island}}}};
  auto touchingPrimitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(
          Primitive::Operation::Union, {containingShell}));
  auto touching = touchingPrimitive->createEditingProxy();
  auto touchingMappings = touching->getNodeMappings();
  auto islandMapping = std::find_if(
      touchingMappings.begin(), touchingMappings.end(), [](auto const& item) {
        return item.role ==
               bw::core::MeshPrimitiveEditingProxy::NodeRole::Island;
      });
  require(islandMapping != touchingMappings.end(),
          "the touching-Vertex fixture had no Island");
  auto islandVertices =
      touching->getPolygon(islandMapping->polygonIndex).getOrderedVertexIndices();
  uint32_t shared = ~0u;
  for (auto vertexIndex : islandVertices) {
    if (touching->getVertex(vertexIndex).getEdgeReferences().size() > 2) {
      shared = vertexIndex;
      break;
    }
  }
  require(shared != ~0u, "the touching Island did not share its Vertex");
  auto sharedAt = std::find(islandVertices.begin(), islandVertices.end(), shared);
  auto opposite = islandVertices[(static_cast<size_t>(sharedAt - islandVertices.begin()) + 2) %
                                 islandVertices.size()];
  require(touching->sliceFilledRing(
              islandMapping->polygonIndex, shared, opposite),
          "Slice rejected a Vertex shared with an owning Hole/Island family");
  touching->commitTo(*touchingPrimitive);
  require(touchingPrimitive->getShells()[0].holes[0].islands.size() == 2,
          "slicing the touching Island did not create two sibling Islands");
}

void sliceMarksTheCreatedInternalEdgeNonColliding() {
  ClosedPolygon shell{
      {{-10, -10}}, {{0, -10}}, {{10, -10}}, {{10, 10}}, {{0, 10}}, {{-10, 10}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{shell, {}}}));
  auto proxy = primitive->createEditingProxy();
  auto ordered = proxy->getPolygon(proxy->getFirstPolygonIndex())
                     .getOrderedVertexIndices();
  auto bottom = ordered[1];
  auto top = ordered[4];

  require(proxy->sliceFilledRing(
              proxy->getFirstPolygonIndex(), bottom, top),
          "the internal-edge collision fixture was not sliced");
  proxy->commitTo(*primitive);

  auto const& parts = primitive->getShells();
  require(parts.size() == 2,
          "the collision fixture Slice did not create two Rings");
  size_t sharedEdgeCount = 0;
  for (size_t first = 0; first < parts[0].ring.size(); ++first) {
    auto const& firstVertex = parts[0].ring[first];
    auto const& firstNext =
        parts[0].ring[(first + 1) % parts[0].ring.size()];
    for (size_t second = 0; second < parts[1].ring.size(); ++second) {
      auto const& secondVertex = parts[1].ring[second];
      auto const& secondNext =
          parts[1].ring[(second + 1) % parts[1].ring.size()];
      if (firstVertex.p == secondNext.p && firstNext.p == secondVertex.p) {
        ++sharedEdgeCount;
        auto collisionBits =
            BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG | BW_MESH_EDGE_COLLIDES_FLAG;
        require((firstVertex.edgeFlags & collisionBits) == 0 &&
                    (secondVertex.edgeFlags & collisionBits) == 0,
                "Slice authored a collision override on its Internal edge");
      }
    }
  }
  require(sharedEdgeCount == 1,
          "the resulting Rings did not share exactly the Slice chord");
}

// The Vertices a chord divides a Ring at end up on both halves, joined by the
// chord Edge. Slicing again from one of them is ordinary: the new chord meets
// the old one at the Vertex they share, which is contact at an endpoint and
// not an obstruction. Refusing it left a Vertex unusable for good the moment
// it was sliced through once.
void sliceAcceptsASecondChordFromAnEndpointOfTheFirst() {
  auto vertexAt = [](MeshPrimitiveEditingProxy const& proxy,
                     wp::Vector2 const& position) {
    for (auto ring = proxy.getFirstPolygonIndex();
         !proxy.polygonIndexIterationFinished(ring);
         ring = proxy.getNextPolygonIndex(ring)) {
      for (auto vertexIndex : proxy.getPolygon(ring).getOrderedVertexIndices()) {
        auto const& candidate = proxy.getVertex(vertexIndex).getPosition();
        if (near(candidate.x, position.x) && near(candidate.y, position.y)) {
          return vertexIndex;
        }
      }
    }
    return ~0u;
  };
  auto ringContaining = [](MeshPrimitiveEditingProxy const& proxy,
                           uint32_t firstVertex, uint32_t secondVertex) {
    for (auto const& mapping : proxy.getNodeMappings()) {
      auto const& vertices =
          proxy.getPolygon(mapping.polygonIndex).getVertexIndexSet();
      if (vertices.contains(firstVertex) && vertices.contains(secondVertex)) {
        return mapping.polygonIndex;
      }
    }
    return ~0u;
  };

  ClosedPolygon hexagon{
      {{10, 0}}, {{5, 9}}, {{-5, 9}}, {{-10, 0}}, {{-5, -9}}, {{5, -9}}};

  // The first chord halves the hexagon between opposite corners. The second
  // starts from one of those two corners, and has to be accepted whichever
  // half it falls in - so run it once into each.
  for (auto secondTarget : {size_t(2), size_t(4)}) {
    auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
        Primitive::Operation::Union, {{hexagon, {}}}));
    auto proxy = primitive->createEditingProxy();
    auto ordered = proxy->getPolygon(proxy->getFirstPolygonIndex())
                       .getOrderedVertexIndices();
    require(ordered.size() == 6, "the hexagon fixture lost a corner");

    // Positions rather than indices: the proxy is rebuilt by a Slice, and it
    // is the corner in the world that is being clicked twice, not an index.
    auto sharedPosition = proxy->getVertex(ordered[0]).getPosition();
    auto oppositePosition = proxy->getVertex(ordered[3]).getPosition();
    auto targetPosition = proxy->getVertex(ordered[secondTarget]).getPosition();

    require(
        proxy->sliceFilledRing(
            proxy->getFirstPolygonIndex(), ordered[0], ordered[3]),
        "the first chord across the hexagon was refused");

    auto shared = vertexAt(*proxy, sharedPosition);
    auto target = vertexAt(*proxy, targetPosition);
    auto opposite = vertexAt(*proxy, oppositePosition);
    require(
        shared != ~0u && target != ~0u && opposite != ~0u,
        "the halves did not keep the corners the chord divided them at");
    require(
        ringContaining(*proxy, shared, opposite) != ~0u,
        "the chord's own endpoints did not stay on a shared Ring");

    auto ring = ringContaining(*proxy, shared, target);
    require(ring != ~0u, "the second chord's endpoints shared no Ring");
    require(
        proxy->sliceFilledRing(ring, shared, target),
        "a second chord from an endpoint of the first was refused");
    proxy->commitTo(*primitive);
    require(
        primitive->getShells().size() == 3,
        "the second chord did not divide one of the halves");
  }
}

void externalEdgesHaveTriStateCollisionOverrides() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{ring(-2, -1, 0, 1), {}}, {ring(0, -1, 2, 1), {}}}));
  auto proxy = primitive->createEditingProxy();

  uint32_t internalEdge = ~0u;
  uint32_t externalEdge = ~0u;
  for (auto edge = proxy->getFirstEdgeIndex();
       !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge)) {
    if (proxy->getEdge(edge).getPolygonReferences().size() == 2) {
      internalEdge = edge;
    } else {
      externalEdge = edge;
    }
  }
  require(internalEdge != ~0u && externalEdge != ~0u,
          "the fixture did not produce both an Internal and an External edge");

  require(!proxy->getEdgeCollisionOverride(externalEdge).has_value(),
          "an External edge did not default to an unset collision override");
  require(proxy->isEdgeCollisionEditable(externalEdge),
          "an External edge was not reported as editable");
  require(!proxy->getEdgeCollisionOverride(internalEdge).has_value(),
          "an Internal edge reported a collision override");
  require(!proxy->isEdgeCollisionEditable(internalEdge),
          "an Internal edge was reported as editable");

  require(proxy->setEdgeCollisionOverride(externalEdge, false),
          "setting doesn't-collide was refused on an External edge");
  require(proxy->getEdgeCollisionOverride(externalEdge) == false,
          "the doesn't-collide override was not retained");
  require(proxy->setEdgeCollisionOverride(externalEdge, true),
          "setting collides was refused on an External edge");
  require(proxy->getEdgeCollisionOverride(externalEdge) == true,
          "the collides override was not retained");
  require(proxy->setEdgeCollisionOverride(externalEdge, std::nullopt),
          "clearing the collision override was refused");
  require(!proxy->getEdgeCollisionOverride(externalEdge).has_value(),
          "clearing the collision override did not restore unset");

  require(!proxy->setEdgeCollisionOverride(internalEdge, true),
          "setting a collision override succeeded on an Internal edge");
  require(!proxy->getEdgeCollisionOverride(internalEdge).has_value(),
          "an Internal edge gained an override after a refused edit");
}

void splitEdgeInheritsCollisionOverrideForBothHalves() {
  for (std::optional<bool> sourceValue : std::array<std::optional<bool>, 3>{
           true, false, std::nullopt}) {
    auto primitive = std::unique_ptr<MeshPrimitive>(
        MeshPrimitive::fromTree(Primitive::Operation::Union, {{ring(-2, -2, 2, 2), {}}}));
    auto proxy = primitive->createEditingProxy();
    auto edgeIndex = proxy->getFirstEdgeIndex();
    require(proxy->setEdgeCollisionOverride(edgeIndex, sourceValue),
            "could not author the source edge's collides value before splitting");

    wp::geometry::SplitEdgeResult split;
    require(proxy->splitEdge(edgeIndex, 0.5f, &split),
            "splitting an External edge was refused");
    require(split.newEdgeIndices.size() == 2,
            "splitEdge did not report exactly two resulting edges");
    require(proxy->getEdgeCollisionOverride(split.newEdgeIndices[0]) == sourceValue &&
                proxy->getEdgeCollisionOverride(split.newEdgeIndices[1]) == sourceValue,
            "splitEdge did not inherit the original edge's collides value on both halves");
  }
}

// removeVertex is implemented by re-deriving the authored Ring vertex list,
// mutating it, and fully rebuilding the proxy Mesh from scratch - so Mesh
// vertex/edge indices are not stable across the call. Vertices are relocated
// by position afterwards.
uint32_t findVertexNear(wp::geometry::Mesh const& mesh, wp::Vector2 const& position) {
  for (auto index = mesh.getFirstVertexIndex();
       !mesh.vertexIndexIterationFinished(index);
       index = mesh.getNextVertexIndex(index)) {
    auto const& candidate = mesh.getVertex(index).getPosition();
    if (near(candidate.x, position.x) && near(candidate.y, position.y)) {
      return index;
    }
  }
  return ~0u;
}

void checkRemoveVertexMerge(bool predecessorValue, bool successorValue) {
  // A pentagon so the middle vertex being removed has distinct, unambiguous
  // predecessor and successor edges.
  ClosedPolygon pentagon{{{-2, 0}}, {{-1, -2}}, {{1, -2}}, {{2, 0}}, {{0, 2}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {{pentagon, {}}}));
  auto proxy = primitive->createEditingProxy();

  auto ordered =
      proxy->getPolygon(proxy->getFirstPolygonIndex()).getOrderedVertexIndices();
  require(ordered.size() == 5, "the pentagon fixture did not retain five vertices");

  // Middle vertex to remove, with its predecessor and successor.
  auto removedVertex = ordered[1];
  auto predecessorVertex = ordered[0];
  auto successorVertex = ordered[2];
  auto predecessorPosition = proxy->getVertex(predecessorVertex).getPosition();
  auto successorPosition = proxy->getVertex(successorVertex).getPosition();

  auto predecessorEdge = proxy->getMesh().getEdgeIndexByVertices(predecessorVertex, removedVertex);
  auto successorEdge = proxy->getMesh().getEdgeIndexByVertices(removedVertex, successorVertex);
  require(predecessorEdge >= 0 && successorEdge >= 0,
          "could not locate the predecessor/successor edges around the removed vertex");

  require(proxy->setEdgeCollisionOverride(static_cast<uint32_t>(predecessorEdge), predecessorValue),
          "could not author the predecessor edge's collides value");
  require(proxy->setEdgeCollisionOverride(static_cast<uint32_t>(successorEdge), successorValue),
          "could not author the successor edge's collides value");

  require(proxy->removeVertex(removedVertex), "removing the middle vertex was refused");

  auto mergedOrdered =
      proxy->getPolygon(proxy->getFirstPolygonIndex()).getOrderedVertexIndices();
  require(mergedOrdered.size() == 4, "removing a vertex did not shrink the ring by one");

  auto rebuiltPredecessor = findVertexNear(proxy->getMesh(), predecessorPosition);
  auto rebuiltSuccessor = findVertexNear(proxy->getMesh(), successorPosition);
  require(rebuiltPredecessor != ~0u && rebuiltSuccessor != ~0u,
          "the predecessor/successor vertices did not survive removal");
  auto mergedEdge =
      proxy->getMesh().getEdgeIndexByVertices(rebuiltPredecessor, rebuiltSuccessor);
  require(mergedEdge >= 0, "the merged edge between predecessor and successor was not found");
  require(proxy->getEdgeCollisionOverride(static_cast<uint32_t>(mergedEdge)) == predecessorValue,
          "the merged edge did not keep the predecessor edge's collides value, "
          "unaffected by the successor edge's discarded value");
}

void removeVertexMergeKeepsThePredecessorEdgesValue() {
  checkRemoveVertexMerge(true, false);
  checkRemoveVertexMerge(false, true);
}

void externalEdgesDefaultVisibleAndInternalEdgesCannotBeSet() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{ring(-2, -1, 0, 1), {}}, {ring(0, -1, 2, 1), {}}}));
  auto proxy = primitive->createEditingProxy();

  uint32_t internalEdge = ~0u;
  uint32_t externalEdge = ~0u;
  for (auto edge = proxy->getFirstEdgeIndex();
       !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge)) {
    if (proxy->getEdge(edge).getPolygonReferences().size() == 2) {
      internalEdge = edge;
    } else {
      externalEdge = edge;
    }
  }
  require(internalEdge != ~0u && externalEdge != ~0u,
          "the fixture did not produce both an Internal and an External edge");

  require(proxy->getEdgeVisible(externalEdge),
          "an External edge did not default to visible = true");
  require(proxy->isEdgeVisibilityEditable(externalEdge),
          "an External edge was not reported as visibility-editable");
  require(!proxy->getEdgeVisible(internalEdge),
          "an Internal edge reported visible = true");
  require(!proxy->isEdgeVisibilityEditable(internalEdge),
          "an Internal edge was reported as visibility-editable");

  require(proxy->setEdgeVisible(externalEdge, false),
          "setEdgeVisible was refused on an External edge");
  require(!proxy->getEdgeVisible(externalEdge),
          "setEdgeVisible(false) did not clear the effective value");
  require(proxy->setEdgeVisible(externalEdge, true),
          "setEdgeVisible was refused when re-enabling an External edge");
  require(proxy->getEdgeVisible(externalEdge),
          "setEdgeVisible(true) did not restore the effective value");

  require(!proxy->setEdgeVisible(internalEdge, true),
          "setEdgeVisible succeeded on an Internal edge");
  require(!proxy->getEdgeVisible(internalEdge),
          "an Internal edge became visible after a refused setEdgeVisible");
}

void normalMapsAreExternalOnlyAndSplitsInheritThem() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{ring(-2, -1, 0, 1), {}}, {ring(0, -1, 2, 1), {}}}));
  auto proxy = primitive->createEditingProxy();
  uint32_t internalEdge = ~0u, externalEdge = ~0u;
  for (auto edge = proxy->getFirstEdgeIndex();
       !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge)) {
    (proxy->getEdge(edge).getConnectivity() == wp::geometry::Edge::Internal
         ? internalEdge
         : externalEdge) = edge;
  }
  auto image = bw::core::WallNormalMapOverride::image(
      "normal/directional.png", 8.0f, 1.25f);
  require(proxy->getEdgeNormalMapOverride(internalEdge).state() ==
                  bw::core::WallNormalMapOverride::State::Unset &&
              !proxy->isEdgeNormalMapEditable(internalEdge) &&
              !proxy->setEdgeNormalMapOverride(internalEdge, image),
          "an Internal edge accepted or exposed authored normal-map state");
  require(proxy->isEdgeNormalMapEditable(externalEdge) &&
              proxy->setEdgeNormalMapOverride(externalEdge, image),
          "an External edge rejected an Image normal-map state");

  wp::geometry::SplitEdgeResult split;
  require(proxy->splitEdge(externalEdge, 0.5f, &split) &&
              split.newEdgeIndices.size() == 2,
          "mapped External edge did not split");
  for (auto edge : split.newEdgeIndices) {
    require(proxy->getEdgeNormalMapOverride(edge) == image,
            "mapped edge split did not inherit the complete Image state");
  }
}

void normalMapStatesSurviveSplitAndProxyRoundTrip() {
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {{ring(-2, -2, 2, 2), {}}}));
  auto proxy = primitive->createEditingProxy();
  std::array values{
      bw::core::WallNormalMapOverride::unset(),
      bw::core::WallNormalMapOverride::disabled(),
      bw::core::WallNormalMapOverride::image("normal/split.png", 6.0f, 0.5f)};
  for (auto value : values) {
    auto edge = proxy->getFirstEdgeIndex();
    require(proxy->setEdgeNormalMapOverride(edge, value),
            "could not author a normal-map state before splitting");
    require(proxy->setEdgeCollisionOverride(edge, false) &&
                proxy->setEdgeVisible(edge, false),
            "could not establish independent edge overrides before splitting");
    wp::geometry::SplitEdgeResult split;
    require(proxy->splitEdge(edge, 0.5f, &split) && split.newEdgeIndices.size() == 2,
            "an External edge did not split");
    for (auto splitEdge : split.newEdgeIndices) {
      require(proxy->getEdgeNormalMapOverride(splitEdge) == value &&
                  proxy->getEdgeCollisionOverride(splitEdge) == false &&
                  !proxy->getEdgeVisible(splitEdge),
              "splitting disturbed a complete wall-edge override state");
    }
    proxy->commitTo(*primitive);
    proxy = primitive->createEditingProxy();
  }
}

void normalMapMergeRefusesDifferentValuesAndPreservesEqualValues() {
  auto makeProxy = [] {
    ClosedPolygon pentagon{{{-2, 0}}, {{-1, -2}}, {{1, -2}}, {{2, 0}}, {{0, 2}}};
    auto primitive = std::unique_ptr<MeshPrimitive>(
        MeshPrimitive::fromTree(Primitive::Operation::Union, {{pentagon, {}}}));
    auto proxy = primitive->createEditingProxy();
    return std::pair{std::move(primitive), std::move(proxy)};
  };
  auto image = bw::core::WallNormalMapOverride::image("normal/merge.png", 4.0f, 1.0f);

  {
    auto [primitive, proxy] = makeProxy();
    auto ordered = proxy->getPolygon(proxy->getFirstPolygonIndex()).getOrderedVertexIndices();
    auto incoming = proxy->getMesh().getEdgeIndexByVertices(ordered[0], ordered[1]);
    auto outgoing = proxy->getMesh().getEdgeIndexByVertices(ordered[1], ordered[2]);
    require(incoming >= 0 && outgoing >= 0 &&
                proxy->setEdgeNormalMapOverride(static_cast<uint32_t>(incoming), image) &&
                proxy->setEdgeNormalMapOverride(static_cast<uint32_t>(outgoing), image),
            "could not author equal normal-map values before merging");
    auto firstPosition = proxy->getVertex(ordered[0]).getPosition();
    auto secondPosition = proxy->getVertex(ordered[2]).getPosition();
    require(proxy->removeVertex(ordered[1]),
            "equal normal-map values should permit an edge merge");
    auto merged = proxy->getMesh().getEdgeIndexByVertices(
        findVertexNear(proxy->getMesh(), firstPosition),
        findVertexNear(proxy->getMesh(), secondPosition));
    require(merged >= 0 &&
                proxy->getEdgeNormalMapOverride(static_cast<uint32_t>(merged)) == image,
            "an equal normal-map merge did not preserve its value");
  }
  {
    auto [primitive, proxy] = makeProxy();
    auto ordered = proxy->getPolygon(proxy->getFirstPolygonIndex()).getOrderedVertexIndices();
    auto incoming = proxy->getMesh().getEdgeIndexByVertices(ordered[0], ordered[1]);
    auto outgoing = proxy->getMesh().getEdgeIndexByVertices(ordered[1], ordered[2]);
    auto before = proxy->getPolygon(proxy->getFirstPolygonIndex()).getOrderedVertexIndices();
    require(incoming >= 0 && outgoing >= 0 &&
                proxy->setEdgeNormalMapOverride(static_cast<uint32_t>(incoming), image) &&
                proxy->setEdgeNormalMapOverride(
                    static_cast<uint32_t>(outgoing),
                    bw::core::WallNormalMapOverride::disabled()),
            "could not author conflicting normal-map values before merging");
    require(!proxy->removeVertex(ordered[1]),
            "a merge with conflicting normal-map values was not refused");
    require(proxy->getPolygon(proxy->getFirstPolygonIndex()).getOrderedVertexIndices() == before &&
                proxy->getEdgeNormalMapOverride(static_cast<uint32_t>(incoming)) == image &&
                proxy->getEdgeNormalMapOverride(static_cast<uint32_t>(outgoing)) ==
                    bw::core::WallNormalMapOverride::disabled(),
            "a refused normal-map merge changed geometry or authored values");
  }
}

void windingNormalizationKeepsNormalMapsOnTheirGeometricEdges() {
  auto image = bw::core::WallNormalMapOverride::image("normal/winding.png", 2.0f, 0.75f);
  ClosedPolygon clockwise{{{-2, -2}}, {{-2, 2}}, {{2, 2}}, {{2, -2}}};
  clockwise[0].edgeNormalMap = image;  // geometric segment (-2,-2) to (-2,2)
  clockwise[1].edgeNormalMap = bw::core::WallNormalMapOverride::disabled();
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {{clockwise, {}}}));
  auto proxy = primitive->createEditingProxy();
  auto const& normalized = primitive->getShells()[0].ring;
  require(normalized[2].edgeNormalMap == image,
          "winding normalization moved a normal map off its geometric edge");
  size_t mappedEdges = 0;
  for (auto edge = proxy->getFirstEdgeIndex();
       !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge)) {
    mappedEdges += proxy->getEdgeNormalMapOverride(edge) == image;
  }
  require(mappedEdges == 1,
          "the normalized geometric edge did not reach the editing proxy");
}

void wallMasksAreExternalOnlyAndSplitsInheritThem() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{ring(-2, -1, 0, 1), {}}, {ring(0, -1, 2, 1), {}}}));
  auto proxy = primitive->createEditingProxy();
  uint32_t internalEdge = ~0u, externalEdge = ~0u;
  for (auto edge = proxy->getFirstEdgeIndex();
       !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge)) {
    (proxy->getEdge(edge).getConnectivity() == wp::geometry::Edge::Internal
         ? internalEdge
         : externalEdge) = edge;
  }
  auto mask = bw::core::WallMaskOverride::image(
      "mask/directional.png", 1,
      {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f});
  require(proxy->getEdgeWallMaskOverride(internalEdge).state() ==
                  bw::core::WallMaskOverride::State::Unset &&
              !proxy->isEdgeWallMaskEditable(internalEdge) &&
              !proxy->setEdgeWallMaskOverride(internalEdge, mask),
          "an Internal edge accepted or exposed authored Wall-mask state");
  require(proxy->isEdgeWallMaskEditable(externalEdge) &&
              proxy->setEdgeWallMaskOverride(externalEdge, mask),
          "an External edge rejected an Image Wall-mask state");

  wp::geometry::SplitEdgeResult split;
  require(proxy->splitEdge(externalEdge, 0.5f, &split) &&
              split.newEdgeIndices.size() == 2,
          "masked External edge did not split");
  for (auto edge : split.newEdgeIndices) {
    require(proxy->getEdgeWallMaskOverride(edge) == mask,
            "masked edge split did not inherit the complete Image state");
  }
}

void wallMaskStatesSurviveSplitAndProxyRoundTrip() {
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {{ring(-2, -2, 2, 2), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto const blend = bw::core::WallMaskOverride::BlendParameters{
      0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
  std::array values{
      bw::core::WallMaskOverride::unset(),
      bw::core::WallMaskOverride::disabled(),
      bw::core::WallMaskOverride::image("mask/split.png", 3, blend)};
  for (auto value : values) {
    auto edge = proxy->getFirstEdgeIndex();
    require(proxy->setEdgeWallMaskOverride(edge, value),
            "could not author a Wall-mask state before splitting");
    require(proxy->setEdgeCollisionOverride(edge, false) &&
                proxy->setEdgeVisible(edge, false),
            "could not establish independent edge overrides before splitting");
    wp::geometry::SplitEdgeResult split;
    require(proxy->splitEdge(edge, 0.5f, &split) && split.newEdgeIndices.size() == 2,
            "an External edge did not split");
    for (auto splitEdge : split.newEdgeIndices) {
      require(proxy->getEdgeWallMaskOverride(splitEdge) == value &&
                  proxy->getEdgeCollisionOverride(splitEdge) == false &&
                  !proxy->getEdgeVisible(splitEdge),
              "splitting disturbed a complete wall-edge override state");
    }
    proxy->commitTo(*primitive);
    proxy = primitive->createEditingProxy();
  }
}

void splitEdgeInheritsVisibleForBothHalves() {
  for (bool sourceValue : {true, false}) {
    auto primitive = std::unique_ptr<MeshPrimitive>(
        MeshPrimitive::fromTree(Primitive::Operation::Union, {{ring(-2, -2, 2, 2), {}}}));
    auto proxy = primitive->createEditingProxy();
    auto edgeIndex = proxy->getFirstEdgeIndex();
    require(proxy->setEdgeVisible(edgeIndex, sourceValue),
            "could not author the source edge's visible value before splitting");

    wp::geometry::SplitEdgeResult split;
    require(proxy->splitEdge(edgeIndex, 0.5f, &split),
            "splitting an External edge was refused");
    require(split.newEdgeIndices.size() == 2,
            "splitEdge did not report exactly two resulting edges");
    require(proxy->getEdgeVisible(split.newEdgeIndices[0]) == sourceValue &&
                proxy->getEdgeVisible(split.newEdgeIndices[1]) == sourceValue,
            "splitEdge did not inherit the original edge's visible value on both halves");
  }
}

void checkRemoveVertexVisibleMerge(bool predecessorValue, bool successorValue) {
  ClosedPolygon pentagon{{{-2, 0}}, {{-1, -2}}, {{1, -2}}, {{2, 0}}, {{0, 2}}};
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {{pentagon, {}}}));
  auto proxy = primitive->createEditingProxy();

  auto ordered =
      proxy->getPolygon(proxy->getFirstPolygonIndex()).getOrderedVertexIndices();
  require(ordered.size() == 5, "the pentagon fixture did not retain five vertices");

  auto removedVertex = ordered[1];
  auto predecessorVertex = ordered[0];
  auto successorVertex = ordered[2];
  auto predecessorPosition = proxy->getVertex(predecessorVertex).getPosition();
  auto successorPosition = proxy->getVertex(successorVertex).getPosition();

  auto predecessorEdge = proxy->getMesh().getEdgeIndexByVertices(predecessorVertex, removedVertex);
  auto successorEdge = proxy->getMesh().getEdgeIndexByVertices(removedVertex, successorVertex);
  require(predecessorEdge >= 0 && successorEdge >= 0,
          "could not locate the predecessor/successor edges around the removed vertex");

  require(proxy->setEdgeVisible(static_cast<uint32_t>(predecessorEdge), predecessorValue),
          "could not author the predecessor edge's visible value");
  require(proxy->setEdgeVisible(static_cast<uint32_t>(successorEdge), successorValue),
          "could not author the successor edge's visible value");

  require(proxy->removeVertex(removedVertex), "removing the middle vertex was refused");

  auto rebuiltPredecessor = findVertexNear(proxy->getMesh(), predecessorPosition);
  auto rebuiltSuccessor = findVertexNear(proxy->getMesh(), successorPosition);
  require(rebuiltPredecessor != ~0u && rebuiltSuccessor != ~0u,
          "the predecessor/successor vertices did not survive removal");
  auto mergedEdge =
      proxy->getMesh().getEdgeIndexByVertices(rebuiltPredecessor, rebuiltSuccessor);
  require(mergedEdge >= 0, "the merged edge between predecessor and successor was not found");
  require(proxy->getEdgeVisible(static_cast<uint32_t>(mergedEdge)) == predecessorValue,
          "the merged edge did not keep the predecessor edge's visible value, "
          "unaffected by the successor edge's discarded value");
}

void removeVertexMergeKeepsThePredecessorEdgesVisibleValue() {
  checkRemoveVertexVisibleMerge(true, false);
  checkRemoveVertexVisibleMerge(false, true);
}

void collidesAndVisibleAreIndependentPerEdge() {
  auto primitive = std::unique_ptr<MeshPrimitive>(
      MeshPrimitive::fromTree(Primitive::Operation::Union, {{ring(-2, -2, 2, 2), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto edgeIndex = proxy->getFirstEdgeIndex();

  require(proxy->setEdgeCollisionOverride(edgeIndex, false) && proxy->setEdgeVisible(edgeIndex, true),
          "could not author collides = false, visible = true on the same edge");
  require(proxy->getEdgeCollisionOverride(edgeIndex) == false && proxy->getEdgeVisible(edgeIndex),
          "setting collides false disturbed the independently-set visible value");

  require(proxy->setEdgeCollisionOverride(edgeIndex, true) && proxy->setEdgeVisible(edgeIndex, false),
          "could not author collides = true, visible = false on the same edge");
  require(proxy->getEdgeCollisionOverride(edgeIndex) == true && !proxy->getEdgeVisible(edgeIndex),
          "setting visible false disturbed the independently-set collides value");
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

void fillRuleIsFixedToEvenOddAndRejectsConflictingAssignment() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{ring(-1, -1, 1, 1), {}}}));
  require(primitive->getFillRule() == Primitive::FillRule::EvenOdd,
          "a tree-native MeshPrimitive did not report EvenOdd semantics");
  primitive->setFillRule(Primitive::FillRule::EvenOdd);

  bool rejected = false;
  try {
    Primitive* generic = primitive.get();
    generic->setFillRule(Primitive::FillRule::NonZero);
  } catch (std::exception const&) {
    rejected = true;
  }
  require(rejected && primitive->getFillRule() == Primitive::FillRule::EvenOdd,
          "a generic non-EvenOdd assignment changed or silently ignored MeshPrimitive meaning");
}

void shallowConversionRejectsCrossEntryNestingAndMalformedTrees() {
  bool rejectedNesting = false;
  try {
    std::unique_ptr<MeshPrimitive> invalid(MeshPrimitive::fromComplexPolygons(
        Primitive::Operation::Union,
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
    removingTwoSidedEdgeMergesSiblingRings();
    fillHoleWrapsImmediateIslandsWithoutLosingDescendants();
    sliceDividesFilledRingsAndRetainsHoles();
    sliceMarksTheCreatedInternalEdgeNonColliding();
    sliceAcceptsASecondChordFromAnEndpointOfTheFirst();
    externalEdgesHaveTriStateCollisionOverrides();
    splitEdgeInheritsCollisionOverrideForBothHalves();
    removeVertexMergeKeepsThePredecessorEdgesValue();
    externalEdgesDefaultVisibleAndInternalEdgesCannotBeSet();
    normalMapsAreExternalOnlyAndSplitsInheritThem();
    normalMapStatesSurviveSplitAndProxyRoundTrip();
    normalMapMergeRefusesDifferentValuesAndPreservesEqualValues();
    windingNormalizationKeepsNormalMapsOnTheirGeometricEdges();
    wallMasksAreExternalOnlyAndSplitsInheritThem();
    wallMaskStatesSurviveSplitAndProxyRoundTrip();
    splitEdgeInheritsVisibleForBothHalves();
    removeVertexMergeKeepsThePredecessorEdgesVisibleValue();
    collidesAndVisibleAreIndependentPerEdge();
    failedProxyCommitLeavesAuthoredAndDerivedGeometryUnchanged();
    fillRuleIsFixedToEvenOddAndRejectsConflictingAssignment();
    shallowConversionRejectsCrossEntryNestingAndMalformedTrees();
    std::cout << "MeshPrimitive geometry proxy tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
