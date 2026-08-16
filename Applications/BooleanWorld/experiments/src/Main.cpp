#include <iostream>
#include <cmath>
#include <filesystem>
#include <random>
#include <type_traits>

#include <gtest/gtest.h>

#include <willpower/common/Timer.h>

#include <core/YamlSerializer.h>
#include <core/World.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/Clipper2Polygon.h>
#include <core/Arrangement.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/RectanglePolygon.h>

#include "GeometryComparison.h"

using namespace std;

shared_ptr<bw::core::World> createWorld(float size, float gridSize) {
  auto world = make_shared<bw::core::World>(size, gridSize);

  auto genFn = [world](wp::Vector2 offset, int dimX, int dimY, float cellSize) {
    auto wdg = new bw::core::DynamicWorldDataGenerator(world.get());

    wdg->setAlwaysUpdateVertices(true);
    wdg->setAllowCommitIfVisible(true);

    return wdg;
  };

  world->setWorldDataGeneratorFactory(genFn);

  return world;
}

shared_ptr<bw::core::World> openWorld(string const& filepath) {
  auto path = filesystem::path(filepath);
  auto ext = path.extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  shared_ptr<bw::core::World> world;

  if (ext == ".yaml") {
    auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::fromFile(filepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      cout << e.what() << "\n";
      return nullptr;
    }

    world = createWorld(8192, 8192);

    auto workData = bw::core::SerializationWorkData{};

    if (world->deserialize(ser, workData)) {
      auto const& warnings = world->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          cout << warning << "\n";
        }
      }

      return world;
    } else {
      auto const& errors = world->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          cout << error << "\n";
        }
      }

      return nullptr;
    }
  } else {
    cout << "Unsupported file format.\n";
    return nullptr;
  }
}

bool gClipperAllocatorsInitialized = false;

void ensureClipperAllocatorsInitialized() {
  if (!gClipperAllocatorsInitialized) {
    Clipper2Lib::WmInitialiseAllocators(4, 16 * 1024 * 1024);
    gClipperAllocatorsInitialized = true;
  }
}

TEST(ArrangementWorldDataGenerator, NewPathIsSelectableWithoutChangingLegacyDefault) {
  ensureClipperAllocatorsInitialized();
  auto world = createWorld(8192, 512);

  auto room = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  room->setPosition({0, 0});
  room->setSize(20, 20);
  world->addPrimitive(room);
  world->update(0, {{0, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});

  auto legacy = world->getWorldData({0, 0}, 0);
  bw::core::ArrangementWorldDataGenerator arrangementGenerator;
  arrangementGenerator.generate(world.get());
  auto arrangement = arrangementGenerator.getWorldData();

  EXPECT_FALSE(legacy->getBorderPolygons().empty());
  ASSERT_NE(arrangement, nullptr);
  EXPECT_TRUE(std::any_of(
      arrangement->faces.begin(), arrangement->faces.end(),
      [](expr::ArrangementFace const& face) {
        return face.solid;
      }));
  EXPECT_EQ(
      dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
          world->getWorldDataGenerator()) != nullptr,
      true);
}

TEST(GeometryComparison, SamplesAllStrategiesAndReportsEquivalentWorld) {
  ensureClipperAllocatorsInitialized();
  auto world = createWorld(8192, 512);

  auto room = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  room->setPosition({0, 0});
  room->setSize(20, 20);
  world->addPrimitive(room);

  bw::experiments::GeometryComparisonOptions options;
  options.gridResolution = 4;
  options.randomSampleCount = 8;
  options.edgeSamplesPerEdge = 1;
  auto report = bw::experiments::CompareWorldGeometry(*world, options);

  EXPECT_EQ(
      report.sampleCounts[size_t(bw::experiments::SampleKind::UniformGrid)],
      16u);
  EXPECT_EQ(
      report.sampleCounts[size_t(bw::experiments::SampleKind::Random)],
      8u);
  EXPECT_EQ(
      report.sampleCounts[size_t(bw::experiments::SampleKind::NearEdge)],
      16u);
  EXPECT_DOUBLE_EQ(report.oldSolidArea, 400.0);
  EXPECT_DOUBLE_EQ(report.newSolidArea, 400.0);
  EXPECT_TRUE(report.matches());
}

TEST(GeometryComparison, RepositoryWorldsMatchExceptKnownBadRepros) {
  ensureClipperAllocatorsInitialized();

  struct WorldCase {
    char const* name;
    char const* path;
    bool knownBad;
  };
  std::array worlds{
      WorldCase{"stress-test-1", "../../../../app/resources/stress-test-1.yaml", false},
      WorldCase{"gen-3", "../../../../app/resources/gen-3.yaml", false},
      WorldCase{"basic-test", "../../../../app/resources/basic-test.yaml", false},
      WorldCase{"bug-1", "../../../../app/resources/bug-1.yaml", true},
      WorldCase{"collision-issue-repro", "../../../../app/resources/collision-issue-repro.yaml", true},
      WorldCase{"duplicate-test", "../../../../app/resources/duplicate-test.yaml", false},
      WorldCase{"int-xor-test", "../../../../editor/resources/int-xor-test.yaml", false},
      WorldCase{"z-optimisation", "../../../../app/resources/z-optimisation.yaml", false}};

  bw::experiments::GeometryComparisonOptions options;
  options.gridResolution = 64;
  options.randomSampleCount = 512;
  options.edgeSamplesPerEdge = 2;

  for (auto const& worldCase : worlds) {
    SCOPED_TRACE(worldCase.name);
    auto world = openWorld(worldCase.path);
    ASSERT_NE(world, nullptr);

    auto report = bw::experiments::CompareWorldGeometry(*world, options);
    cout << worldCase.name << ": " << report.disagreements.size()
         << " disagreements across " << report.totalSampleCount()
         << " samples; area old=" << report.oldSolidArea
         << ", new=" << report.newSolidArea << "\n";
    for (size_t i = 0; i < min<size_t>(report.disagreements.size(), 10); ++i) {
      auto const& disagreement = report.disagreements[i];
      cout << "  (" << disagreement.position.x << ", "
           << disagreement.position.y << ") kind="
           << int(disagreement.kind) << " old={"
           << disagreement.oldEngine.solid << ", "
           << disagreement.oldEngine.primitiveIndex << "} new={"
           << disagreement.newEngine.solid << ", "
           << disagreement.newEngine.primitiveIndex << "}";
      for (auto primitiveIndex : {
               disagreement.oldEngine.primitiveIndex,
               disagreement.newEngine.primitiveIndex}) {
        if (primitiveIndex < world->getNumPrimitives()) {
          auto primitive = world->getPrimitive(primitiveIndex);
          cout << " p" << primitiveIndex << "={op="
               << int(primitive->getOperation()) << ", priority="
               << int(primitive->getPriority()) << ", size=("
               << primitive->getSize().x << ", "
               << primitive->getSize().y << ")}";
        }
      }
      cout << "\n";
    }

    if (!worldCase.knownBad) {
      auto areaTolerance = max(0.01, abs(report.oldSolidArea) * 1e-6);
      EXPECT_NEAR(
          report.oldSolidArea,
          report.newSolidArea,
          areaTolerance);
      EXPECT_TRUE(report.matches());
    }
  }
}

TEST(Generation, PublishedSnapshotRemainsCoherentAfterNextGeneration) {
  ensureClipperAllocatorsInitialized();
  auto world = createWorld(8192, 512);

  auto room = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  room->setPosition({0, 0});
  room->setSize(20, 20);
  world->addPrimitive(room);
  world->update(0, {{0, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});

  auto firstGeneration = world->getWorldData({0, 0}, 0);
  ASSERT_FALSE(firstGeneration->getBorderPolygons().empty());
  auto firstPosition = firstGeneration->getBorderPolygons()[0].bounds.getPosition();

  auto distantIntersection = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Intersection,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  distantIntersection->setPosition({1000, 0});
  distantIntersection->setSize(20, 20);
  world->addPrimitive(distantIntersection);

  auto generator = static_cast<bw::core::DynamicWorldDataGenerator*>(
      world->getWorldDataGenerator());
  generator->generateBlocking();
  auto secondGeneration = world->getWorldData({0, 0}, 0);

  ASSERT_NE(firstGeneration.get(), secondGeneration.get());
  EXPECT_TRUE(secondGeneration->getBorderPolygons().empty());
  EXPECT_FALSE(firstGeneration->getBorderPolygons().empty());
  EXPECT_EQ(firstGeneration->getBorderPolygons()[0].bounds.getPosition(), firstPosition);
}

TEST(Generation, GeometryIsIndependentOfViewerPosition) {
  ensureClipperAllocatorsInitialized();

  auto generateAt = [](wp::Vector2 const& viewerPosition) {
    auto world = createWorld(8192, 512);

    auto room = new bw::core::RectanglePolygon(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        1.0f);
    room->setPosition({0, 0});
    room->setSize(20, 20);
    world->addPrimitive(room);

    auto distantIntersection = new bw::core::RectanglePolygon(
        bw::core::Primitive::Operation::Intersection,
        bw::core::Primitive::FillRule::NonZero,
        1.0f);
    distantIntersection->setPosition({1000, 0});
    distantIntersection->setSize(20, 20);
    world->addPrimitive(distantIntersection);

    world->update(
        0,
        {viewerPosition, 0, 1, 60, 256, false, false, 0},
        {100, 100});
    return world->getWorldData(viewerPosition, 0);
  };

  auto generatedAtRoom = generateAt({0, 0});
  auto generatedFarAway = generateAt({3000, 0});

  EXPECT_TRUE(generatedAtRoom->getBorderPolygons().empty());
  EXPECT_TRUE(generatedFarAway->getBorderPolygons().empty());
}

////////////////////////////////////////////////////////////////
// Geometry tests

using namespace Clipper2Lib;
using namespace expr;

static_assert(std::is_same_v<decltype(expr::Vertex::x), int64_t>);

static PSLG BuildTestPSLG(
    std::vector<bw::core::Clipper2Polygon> polygons) {
  for (auto& polygon : polygons) {
    for (auto& point : polygon.path) {
      point.x *= 1000;
      point.y *= 1000;
    }
  }

  return BuildPSLG(polygons, {});
}

static PSLG BuildFixedTestPSLG(
    std::vector<bw::core::Clipper2Polygon> const& polygons,
    std::vector<bw::core::Primitive*> const& primitives = {}) {
  return BuildPSLG(polygons, primitives);
}

static int VertexDegree(PSLG const& graph, expr::Vertex const& vertex) {
  auto vertexIt = std::find(graph.vs.begin(), graph.vs.end(), vertex);
  if (vertexIt == graph.vs.end()) {
    return 0;
  }

  auto vertexIndex = (int)std::distance(graph.vs.begin(), vertexIt);
  return (int)std::count_if(graph.es.begin(), graph.es.end(), [&](expr::Edge const& edge) {
    return edge.vi[0] == vertexIndex || edge.vi[1] == vertexIndex;
  });
}

struct Expected {
  int vertices;
  int edges;
  int cycles;
  int roots;
};

static int CountRoots(
    const std::vector<expr::PolygonNode>& nodes) {
  int count = 0;

  for (auto& n : nodes) {
    if (n.parent == -1)
      ++count;
  }

  return count;
}

static void RunBasicTest(
    std::vector<bw::core::Clipper2Polygon> const& polygons,
    const Expected& expected) {
  // Create graph
  expr::PSLG pslg = BuildTestPSLG(polygons);

  // Get cycles
  auto cycles = expr::ExtractMinimalCycles(pslg);

  // Set faces as being either filled or not

  // Build hierarchy.
  auto hierarchy = expr::BuildPolygonHierarchy(pslg, cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  EXPECT_EQ(
      (int)pslg.vs.size(),
      expected.vertices);

  EXPECT_EQ(
      (int)pslg.es.size(),
      expected.edges);

  EXPECT_EQ(
      (int)faces.size(),
      expected.cycles);

  EXPECT_EQ(
      CountRoots(hierarchy),
      expected.roots);
}

using namespace Clipper2Lib;
using namespace expr;

TEST(ArrangementFold, UnionIncludesMember) {
  std::vector<ArrangementPrimitive> primitives =
      {{{}, bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 10, 100}};
  Membership membership(primitives.size());
  membership.set(0);

  EXPECT_TRUE(EvaluateFold(primitives, membership));
}

TEST(ArrangementFold, IntersectionRequiresMember) {
  std::vector<ArrangementPrimitive> primitives =
      {{{}, bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 0, 100},
       {{}, bw::core::Primitive::Operation::Intersection, bw::core::Primitive::FillRule::NonZero, 1, 101}};
  Membership membership(primitives.size());
  membership.set(0);

  EXPECT_FALSE(EvaluateFold(primitives, membership));

  membership.set(1);
  EXPECT_TRUE(EvaluateFold(primitives, membership));
}

TEST(ArrangementFold, DifferenceRemovesMember) {
  std::vector<ArrangementPrimitive> primitives =
      {{{}, bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 0, 100},
       {{}, bw::core::Primitive::Operation::Difference, bw::core::Primitive::FillRule::NonZero, 1, 101}};
  Membership membership(primitives.size());
  membership.set(0);

  EXPECT_TRUE(EvaluateFold(primitives, membership));

  membership.set(1);
  EXPECT_FALSE(EvaluateFold(primitives, membership));
}

TEST(ArrangementFold, XorTogglesForMember) {
  std::vector<ArrangementPrimitive> primitives =
      {{{}, bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 0, 100},
       {{}, bw::core::Primitive::Operation::XOR, bw::core::Primitive::FillRule::NonZero, 1, 101}};
  Membership membership(primitives.size());
  membership.set(0);
  membership.set(1);

  EXPECT_FALSE(EvaluateFold(primitives, membership));
}

TEST(ArrangementFold, PriorityOverridesInputOrder) {
  std::vector<ArrangementPrimitive> primitives =
      {{{}, bw::core::Primitive::Operation::Difference, bw::core::Primitive::FillRule::NonZero, 20, 100},
       {{}, bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 10, 101}};
  Membership membership(primitives.size());
  membership.set(0);
  membership.set(1);

  EXPECT_FALSE(EvaluateFold(primitives, membership));
}

static ArrangementFace const* FindFaceAt(
    ArrangementResultPtr const& arrangement,
    expr::Vertex const& point) {
  auto face = std::find_if(
      arrangement->faces.begin(), arrangement->faces.end(),
      [&](ArrangementFace const& candidate) {
        return PointInFace(point, candidate, *arrangement);
      });
  return face == arrangement->faces.end() ? nullptr : &*face;
}

TEST(Arrangement, ClassifiesOverlappingPrimitiveFaces) {
  std::vector<ArrangementPrimitive> primitives =
      {{{{{0, 0}, {20, 0}, {20, 20}, {0, 20}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        0,
        100},
       {{{{10, 0}, {30, 0}, {30, 20}, {10, 20}}},
        bw::core::Primitive::Operation::Difference,
        bw::core::Primitive::FillRule::NonZero,
        1,
        101}};

  auto arrangement = BuildArrangement(primitives);
  auto left = FindFaceAt(arrangement, {5, 10});
  auto overlap = FindFaceAt(arrangement, {15, 10});
  auto right = FindFaceAt(arrangement, {25, 10});

  ASSERT_NE(left, nullptr);
  ASSERT_NE(overlap, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_TRUE(left->membership.contains(0));
  EXPECT_FALSE(left->membership.contains(1));
  EXPECT_TRUE(left->solid);
  EXPECT_EQ(left->primitiveIndex, 100u);
  EXPECT_TRUE(overlap->membership.contains(0));
  EXPECT_TRUE(overlap->membership.contains(1));
  EXPECT_FALSE(overlap->solid);
  EXPECT_EQ(overlap->primitiveIndex, 101u);
  EXPECT_FALSE(right->membership.contains(0));
  EXPECT_TRUE(right->membership.contains(1));
  EXPECT_FALSE(right->solid);
  EXPECT_EQ(arrangement->faces.size(), 4u);
}

static ArrangementResultPtr BuildNestedContourArrangement(
    bw::core::Primitive::FillRule fillRule) {
  return BuildArrangement(
      {{{{{0, 0}, {30, 0}, {30, 30}, {0, 30}},
         {{10, 10}, {20, 10}, {20, 20}, {10, 20}}},
        bw::core::Primitive::Operation::Union,
        fillRule,
        0,
        500}});
}

TEST(Arrangement, MultiContourPrimitiveUsesEvenOddFillRule) {
  auto arrangement = BuildNestedContourArrangement(
      bw::core::Primitive::FillRule::EvenOdd);
  auto outer = FindFaceAt(arrangement, {5, 5});
  auto inner = FindFaceAt(arrangement, {15, 15});

  ASSERT_NE(outer, nullptr);
  ASSERT_NE(inner, nullptr);
  EXPECT_TRUE(outer->membership.contains(0));
  EXPECT_TRUE(outer->solid);
  EXPECT_FALSE(inner->membership.contains(0));
  EXPECT_FALSE(inner->solid);
}

TEST(Arrangement, MultiContourPrimitiveUsesNonZeroFillRuleWithoutMerging) {
  auto arrangement = BuildNestedContourArrangement(
      bw::core::Primitive::FillRule::NonZero);
  auto outer = FindFaceAt(arrangement, {5, 5});
  auto inner = FindFaceAt(arrangement, {15, 15});

  ASSERT_NE(outer, nullptr);
  ASSERT_NE(inner, nullptr);
  EXPECT_TRUE(outer->membership.contains(0));
  EXPECT_TRUE(outer->solid);
  EXPECT_TRUE(inner->membership.contains(0));
  EXPECT_TRUE(inner->solid);
  EXPECT_NE(outer, inner);
  EXPECT_EQ(arrangement->faces.size(), 3u);
}

static ArrangementResultPtr BuildSelfIntersectingStarArrangement(
    bw::core::Primitive::FillRule fillRule) {
  return BuildArrangement(
      {{{{{0, 30}, {18, -24}, {-29, 9}, {29, 9}, {-18, -24}}},
        bw::core::Primitive::Operation::Union,
        fillRule,
        0,
        600}});
}

TEST(Arrangement, SelfIntersectingPrimitiveUsesEvenOddFillRule) {
  auto arrangement = BuildSelfIntersectingStarArrangement(
      bw::core::Primitive::FillRule::EvenOdd);
  auto centre = FindFaceAt(arrangement, {0, 0});

  ASSERT_NE(centre, nullptr);
  EXPECT_FALSE(centre->membership.contains(0));
  EXPECT_FALSE(centre->solid);
}

TEST(Arrangement, SelfIntersectingPrimitiveUsesNonZeroFillRule) {
  auto arrangement = BuildSelfIntersectingStarArrangement(
      bw::core::Primitive::FillRule::NonZero);
  auto centre = FindFaceAt(arrangement, {0, 0});

  ASSERT_NE(centre, nullptr);
  EXPECT_TRUE(centre->membership.contains(0));
  EXPECT_TRUE(centre->solid);
}

TEST(ArrangementOutput, FacesOwnExplicitBoundariesAndEdgesOwnBothFaces) {
  auto arrangement = BuildArrangement(
      {{{{{0, 0}, {30, 0}, {30, 30}, {0, 30}},
         {{10, 10}, {20, 10}, {20, 20}, {10, 20}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        0,
        500,
        {}}});

  ASSERT_EQ(arrangement->faces.size(), 3u);
  EXPECT_TRUE(arrangement->faces[0].outerBoundary.empty());
  EXPECT_EQ(arrangement->faces[0].innerBoundaries.size(), 1u);
  auto faceWithHole = std::find_if(
      arrangement->faces.begin(), arrangement->faces.end(),
      [](ArrangementFace const& face) {
        return !face.outerBoundary.empty() &&
               !face.innerBoundaries.empty();
      });
  ASSERT_NE(faceWithHole, arrangement->faces.end());
  EXPECT_EQ(faceWithHole->outerBoundary.size(), 4u);
  ASSERT_EQ(faceWithHole->innerBoundaries.size(), 1u);
  EXPECT_EQ(faceWithHole->innerBoundaries[0].size(), 4u);

  for (auto const& edge : arrangement->edges) {
    EXPECT_LT(edge.v[0], arrangement->vertices.size());
    EXPECT_LT(edge.v[1], arrangement->vertices.size());
    EXPECT_LT(edge.face[0], arrangement->faces.size());
    EXPECT_LT(edge.face[1], arrangement->faces.size());
  }
}

TEST(ArrangementOutput, FacePropertiesAreCopiedIntoImmutablePalette) {
  bw::core::PrimitivePropertySet firstProperties{};
  firstProperties.floorZ = 12;
  firstProperties.ceilingZ = 40;
  bw::core::PrimitivePropertySet secondProperties{};
  secondProperties.floorZ = 24;
  secondProperties.ceilingZ = 64;

  std::vector<ArrangementPrimitive> primitives =
      {{{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        0,
        42,
        firstProperties},
       {{{{20, 0}, {30, 0}, {30, 10}, {20, 10}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        1,
        77,
        secondProperties}};

  auto arrangement = BuildArrangement(primitives);
  static_assert(std::is_const_v<ArrangementResultPtr::element_type>);
  primitives[0].properties.floorZ = 999;

  auto firstFace = std::find_if(
      arrangement->faces.begin(), arrangement->faces.end(),
      [](ArrangementFace const& face) {
        return face.primitiveIndex == 42;
      });
  auto secondFace = std::find_if(
      arrangement->faces.begin(), arrangement->faces.end(),
      [](ArrangementFace const& face) {
        return face.primitiveIndex == 77;
      });

  ASSERT_NE(firstFace, arrangement->faces.end());
  ASSERT_NE(secondFace, arrangement->faces.end());
  EXPECT_EQ(arrangement->palette[firstFace->paletteIndex].floorZ, 12);
  EXPECT_EQ(arrangement->palette[firstFace->paletteIndex].ceilingZ, 40);
  EXPECT_EQ(arrangement->palette[secondFace->paletteIndex].floorZ, 24);
  EXPECT_EQ(arrangement->palette[secondFace->paletteIndex].ceilingZ, 64);
}

TEST(ArrangementOutput, BuildsBorderAndStepWallSpansFromIncidentFaces) {
  bw::core::PrimitivePropertySet lowerProperties{};
  lowerProperties.floorZ = 0;
  lowerProperties.ceilingZ = 10;
  bw::core::PrimitivePropertySet higherProperties{};
  higherProperties.floorZ = 2;
  higherProperties.ceilingZ = 12;

  auto arrangement = BuildArrangement(
      {{{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        0,
        42,
        lowerProperties},
       {{{{10, 0}, {20, 0}, {20, 10}, {10, 10}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        1,
        77,
        higherProperties}});

  auto triangles = BuildArrangementTriangles(*arrangement);
  auto walls = BuildArrangementWalls(*arrangement);
  EXPECT_EQ(triangles.size(), 4u);

  auto borderWall = std::find_if(
      walls.begin(), walls.end(), [&](ArrangementWall const& wall) {
        auto const& edge = arrangement->edges[wall.edge];
        return wall.kind == ArrangementWallKind::Border &&
               arrangement->vertices[edge.v[0]].x == 0 &&
               arrangement->vertices[edge.v[1]].x == 0;
      });
  ASSERT_NE(borderWall, walls.end());
  EXPECT_EQ(borderWall->minZ, 0);
  EXPECT_EQ(borderWall->maxZ, 10);
  EXPECT_EQ(
      arrangement->palette[borderWall->paletteIndex].floorZ,
      0);

  auto sharedEdge = std::find_if(
      arrangement->edges.begin(), arrangement->edges.end(),
      [&](ArrangementEdge const& edge) {
        auto const& a = arrangement->vertices[edge.v[0]];
        auto const& b = arrangement->vertices[edge.v[1]];
        return a.x == 10 && b.x == 10;
      });
  ASSERT_NE(sharedEdge, arrangement->edges.end());
  auto sharedEdgeIndex = uint32_t(sharedEdge - arrangement->edges.begin());

  std::vector<ArrangementWall> sharedWalls;
  std::copy_if(
      walls.begin(), walls.end(), std::back_inserter(sharedWalls),
      [&](ArrangementWall const& wall) {
        return wall.edge == sharedEdgeIndex;
      });
  ASSERT_EQ(sharedWalls.size(), 2u);
  EXPECT_EQ(sharedWalls[0].kind, ArrangementWallKind::FloorStep);
  EXPECT_EQ(sharedWalls[0].minZ, 0);
  EXPECT_EQ(sharedWalls[0].maxZ, 2);
  EXPECT_EQ(sharedWalls[1].kind, ArrangementWallKind::CeilingStep);
  EXPECT_EQ(sharedWalls[1].minZ, 10);
  EXPECT_EQ(sharedWalls[1].maxZ, 12);
  EXPECT_EQ(
      arrangement->palette[sharedWalls[0].paletteIndex].floorZ,
      0);
  EXPECT_EQ(
      arrangement->palette[sharedWalls[1].paletteIndex].ceilingZ,
      12);
}

TEST(ArrangementOutput, MatchingNeighbourHeightsDoNotBuildStepWalls) {
  bw::core::PrimitivePropertySet properties{};
  properties.floorZ = 3;
  properties.ceilingZ = 15;
  auto arrangement = BuildArrangement(
      {{{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        0,
        42,
        properties},
       {{{{10, 0}, {20, 0}, {20, 10}, {10, 10}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        1,
        77,
        properties}});

  auto walls = BuildArrangementWalls(*arrangement);
  auto sharedWall = std::find_if(
      walls.begin(), walls.end(), [&](ArrangementWall const& wall) {
        auto const& edge = arrangement->edges[wall.edge];
        return arrangement->vertices[edge.v[0]].x == 10 &&
               arrangement->vertices[edge.v[1]].x == 10;
      });
  EXPECT_EQ(sharedWall, walls.end());
}

TEST(ArrangementOutput, XorRetainsItsFoldRunOwner) {
  bw::core::PrimitivePropertySet unionProperties{};
  unionProperties.floorZ = 12;
  bw::core::PrimitivePropertySet xorProperties{};
  xorProperties.floorZ = 24;

  auto arrangement = BuildArrangement(
      {{{{{0, 0}, {20, 0}, {20, 20}, {0, 20}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        0,
        42,
        unionProperties},
       {{{{10, 0}, {30, 0}, {30, 20}, {10, 20}}},
        bw::core::Primitive::Operation::XOR,
        bw::core::Primitive::FillRule::NonZero,
        1,
        77,
        xorProperties}});

  auto xorOnlyFace = FindFaceAt(arrangement, {25, 10});
  ASSERT_NE(xorOnlyFace, nullptr);
  EXPECT_TRUE(xorOnlyFace->solid);
  EXPECT_FALSE(xorOnlyFace->membership.contains(0));
  EXPECT_TRUE(xorOnlyFace->membership.contains(1));
  EXPECT_EQ(xorOnlyFace->primitiveIndex, 42u);
  EXPECT_EQ(
      arrangement->palette[xorOnlyFace->paletteIndex].floorZ,
      12);
}

TEST(PSLG, SingleRectangle) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}}};

  RunBasicTest(
      polygons,
      {4,
       4,
       1,
       1});
}

TEST(PSLG, IntersectionsAreSnapRoundedToFixedPointGrid) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {10, 0}, {10, -5}, {0, -5}}},
          {false, ~0u, {{1, -1}, {2, 2}, {3, -1}}}};

  auto graph = BuildTestPSLG(polygons);

  EXPECT_NE(
      std::find(graph.vs.begin(), graph.vs.end(), expr::Vertex{1333, 0}),
      graph.vs.end());
}

TEST(PSLG, SnapRoundingCreatesIncidenceOnPreviouslyUnrelatedEdge) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, 0, {{-10, 1}, {10, 0}, {10, 10}}},
          {false, 1, {{-10, 0}, {10, 1}, {-10, -10}}},
          {false, 2, {{-1, 1}, {1, 1}, {1, 2}, {-1, 2}}}};

  auto graph = BuildFixedTestPSLG(polygons);

  EXPECT_EQ(VertexDegree(graph, {0, 1}), 6);
}

TEST(PSLG, CollinearOverlapsProduceOneConsistentEdgeChain) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, 0, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
          {false, 1, {{5, 0}, {15, 0}, {15, -10}, {5, -10}}}};

  auto graph = BuildFixedTestPSLG(polygons);

  EXPECT_EQ(graph.vs.size(), 8u);
  EXPECT_EQ(graph.es.size(), 9u);
  EXPECT_EQ(VertexDegree(graph, {5, 0}), 3);
  EXPECT_EQ(VertexDegree(graph, {10, 0}), 3);
}

TEST(PSLG, GridQuantumGeometrySurvivesAtEveryWorldExtent) {
  constexpr int64_t extent = 4'096'000;
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, 0, {{-extent, -extent}, {-extent + 1, -extent}, {-extent + 1, -extent + 1}, {-extent, -extent + 1}}},
          {false, 1, {{extent - 1, -extent}, {extent, -extent}, {extent, -extent + 1}, {extent - 1, -extent + 1}}},
          {false, 2, {{-extent, extent - 1}, {-extent + 1, extent - 1}, {-extent + 1, extent}, {-extent, extent}}},
          {false, 3, {{extent - 1, extent - 1}, {extent, extent - 1}, {extent, extent}, {extent - 1, extent}}}};

  auto graph = BuildFixedTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(graph);
  auto hierarchy = BuildPolygonHierarchy(graph, cycles);
  auto faces = BuildFaces(hierarchy, cycles);
  auto triangles = BuildFaceTriangles(faces, cycles, graph);

  EXPECT_EQ(graph.vs.size(), 16u);
  EXPECT_EQ(graph.es.size(), 16u);
  EXPECT_EQ(cycles.size(), 4u);
  EXPECT_EQ(triangles.size(), 8u);
}

static void ExpectSelfIntersectingContourTopology(
    bw::core::Primitive::FillRule fillRule) {
  bw::core::RectanglePolygon primitive(
      bw::core::Primitive::Operation::Union,
      fillRule,
      1.0f);
  std::vector<bw::core::Clipper2Polygon> polygons =
      {{false, 0, {{0, 0}, {2'000, 2'000}, {0, 2'000}, {2'000, 0}}}};

  auto graph = BuildFixedTestPSLG(polygons, {&primitive});
  auto cycles = ExtractMinimalCycles(graph);

  EXPECT_EQ(graph.vs.size(), 5u);
  EXPECT_EQ(graph.es.size(), 6u);
  EXPECT_EQ(cycles.size(), 2u);
}

TEST(PSLG, SelfIntersectingContourWithEvenOddFillRule) {
  ExpectSelfIntersectingContourTopology(
      bw::core::Primitive::FillRule::EvenOdd);
}

TEST(PSLG, SelfIntersectingContourWithNonZeroFillRule) {
  ExpectSelfIntersectingContourTopology(
      bw::core::Primitive::FillRule::NonZero);
}

TEST(PSLG, TwoDisconnectedRectangles) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}},
          {false,
           ~0u,
           {{20, 0},
            {30, 0},
            {30, 10},
            {20, 10}}}};

  RunBasicTest(
      polygons,
      {8,
       8,
       2,
       2});
}

TEST(PSLG, PolygonWithHole) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {20, 0},
            {20, 20},
            {0, 20}}},
          {true,
           ~0u,
           {{5, 5},
            {5, 15},
            {15, 15},
            {15, 5}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  ASSERT_EQ(
      hierarchy.size(),
      2u);
}

TEST(PSLG, PolygonWithIsland) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {20, 0},
            {20, 20},
            {0, 20}}},
          {false,
           ~0u,
           {{5, 5},
            {15, 5},
            {15, 15},
            {5, 15}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  ASSERT_EQ(
      faces.size(),
      3u);

  ASSERT_EQ(
      hierarchy.size(),
      3u);
}

TEST(PSLG, PolygonWithIslandAndHoleInIt) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {20, 0},
            {20, 20},
            {0, 20}}},
          {false,
           ~0u,
           {{5, 5},
            {15, 5},
            {15, 15},
            {5, 15}}},
          {true,
           ~0u,
           {{8, 8},
            {8, 12},
            {12, 12},
            {12, 8}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  ASSERT_EQ(
      faces.size(),
      3u);

  ASSERT_EQ(
      hierarchy.size(),
      3u);
}

TEST(PSLG, TwoPolygonsWithHole) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {100, 0},
            {100, 100},
            {0, 100}}},
          {true,
           ~0u,
           {{10, 10},
            {10, 90},
            {90, 90},
            {90, 10}}},
          {false,
           ~0u,
           {{50, 50},
            {150, 50},
            {150, 150},
            {50, 150}}},
          {true,
           ~0u,
           {{60, 60},
            {60, 140},
            {140, 140},
            {140, 60}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  ASSERT_EQ(
      hierarchy.size(),
      9u);
}

TEST(PSLG, HoleWithIsland) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {30, 0},
            {30, 30},
            {0, 30}}},
          {true,
           ~0u,
           {{5, 5},
            {5, 25},
            {25, 25},
            {25, 5}}},
          {false,
           ~0u,
           {{10, 10},
            {20, 10},
            {20, 20},
            {10, 20}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  ASSERT_EQ(
      hierarchy.size(),
      3u);

  std::vector<int> depths;

  for (auto& n : hierarchy) {
    int depth = 0;
    for (int parent = n.parent; parent >= 0; parent = hierarchy[parent].parent) {
      ++depth;
    }
    depths.push_back(depth);
  }

  std::sort(
      depths.begin(),
      depths.end());

  EXPECT_EQ(depths[0], 0);
  EXPECT_EQ(depths[1], 1);
  EXPECT_EQ(depths[2], 2);
}

TEST(PSLG, TwoHoles) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {40, 0},
            {40, 40},
            {0, 40}}},

          {true,
           ~0u,
           {{5, 5},
            {5, 15},
            {15, 15},
            {15, 5}}},

          {true,
           ~0u,
           {{25, 5},
            {35, 5},
            {35, 15},
            {25, 15}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  EXPECT_EQ(hierarchy.size(), 3);
}

TEST(PSLG, SharedEdge) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}},

          {false,
           ~0u,
           {{10, 0},
            {20, 0},
            {20, 10},
            {10, 10}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      2u);
}

TEST(PSLG, TouchingVertex) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}},

          {false,
           ~0u,
           {{10, 10},
            {20, 10},
            {15, 20}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      2u);
}

TEST(PSLG, FiveLevelHierarchy) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {100, 0}, {100, 100}, {0, 100}}},
          {true, ~0u, {{10, 10}, {10, 90}, {90, 90}, {90, 10}}},
          {false, ~0u, {{20, 20}, {80, 20}, {80, 80}, {20, 80}}},
          {true, ~0u, {{30, 30}, {30, 70}, {70, 70}, {70, 30}}},
          {false, ~0u, {{40, 40}, {60, 40}, {60, 60}, {40, 60}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  ASSERT_EQ(
      hierarchy.size(),
      5u);

  std::vector<int> depths;

  for (auto& n : hierarchy) {
    int depth = 0;
    for (int parent = n.parent; parent >= 0; parent = hierarchy[parent].parent) {
      ++depth;
    }
    depths.push_back(depth);
  }

  std::sort(
      depths.begin(),
      depths.end());

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(
        depths[i],
        i);
  }
}

TEST(PSLG, Grid3x3) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {30, 0},
            {30, 30},
            {0, 30}}},

          {false,
           ~0u,
           {{10, 0},
            {20, 0},
            {20, 30},
            {10, 30}}},

          {false,
           ~0u,
           {{0, 10},
            {30, 10},
            {30, 20},
            {0, 20}}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      9u);
}

TEST(PSLG_Pathological, FourWayVertex) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
          {false, ~0u, {{10, 0}, {20, 0}, {20, 10}, {10, 10}}},
          {false, ~0u, {{0, 10}, {10, 10}, {10, 20}, {0, 20}}},
          {false, ~0u, {{10, 10}, {20, 10}, {20, 20}, {10, 20}}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, TJunction) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {30, 0}, {30, 20}, {0, 20}}},
          {false, ~0u, {{10, 0}, {20, 0}, {20, 10}, {10, 10}}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 2u);
}

TEST(Hierarchy_Pathological, EightChildren) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {100, 0}, {100, 100}, {0, 100}}},
          {false, ~0u, {{20, 20}, {40, 20}, {40, 40}, {20, 40}}},
          {false, ~0u, {{45, 20}, {55, 20}, {55, 40}, {45, 40}}},
          {false, ~0u, {{60, 20}, {80, 20}, {80, 40}, {60, 40}}},
          {false, ~0u, {{20, 45}, {40, 45}, {40, 55}, {20, 55}}},
          {false, ~0u, {{60, 45}, {80, 45}, {80, 55}, {60, 55}}},
          {false, ~0u, {{20, 60}, {40, 60}, {40, 80}, {20, 80}}},
          {false, ~0u, {{45, 60}, {55, 60}, {55, 80}, {45, 80}}},
          {false, ~0u, {{60, 60}, {80, 60}, {80, 80}, {60, 80}}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);
  auto tree = BuildPolygonHierarchy(pslg, cycles);
  auto faces = BuildFaces(tree, cycles);

  EXPECT_GE(tree.size(), 9u);
}

TEST(Hierarchy_Pathological, DeepNesting) {
  std::vector<bw::core::Clipper2Polygon> polygons;

  for (int i = 0; i < 5; ++i) {
    int s = i * 10;

    Path64 p;

    if (i % 2 == 1) {
      p = {
          {s, s},
          {s, 100 - s},
          {100 - s, 100 - s},
          {100 - s, s}};
    } else {
      p = {
          {s, s},
          {100 - s, s},
          {100 - s, 100 - s},
          {s, 100 - s}};
    }

    polygons.push_back(
        {i % 2 == 1,
         ~0u,
         p});
  }

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);
  auto tree = BuildPolygonHierarchy(pslg, cycles);
  auto faces = BuildFaces(tree, cycles);

  ASSERT_EQ(tree.size(), 5u);

  int maxDepth = 0;

  for (auto& n : tree) {
    int depth = 0;
    for (int parent = n.parent; parent >= 0; parent = tree[parent].parent) {
      ++depth;
    }
    maxDepth = std::max(maxDepth, depth);
  }

  EXPECT_EQ(maxDepth, 4);
}

TEST(PSLG_Pathological, SharedEdgeChain) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
          {false, ~0u, {{10, 0}, {20, 0}, {20, 10}, {10, 10}}},
          {false, ~0u, {{20, 0}, {30, 0}, {30, 10}, {20, 10}}},
          {false, ~0u, {{30, 0}, {40, 0}, {40, 10}, {30, 10}}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, Pinwheel) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {50, 50}, {0, 100}}},
          {false, ~0u, {{0, 100}, {50, 50}, {100, 100}}},
          {false, ~0u, {{100, 100}, {50, 50}, {100, 0}}},
          {false, ~0u, {{100, 0}, {50, 50}, {0, 0}}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, ThinSliver) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {1000000, 0},
            {1000000, 1},
            {0, 1}}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 1u);
}

TEST(PSLG_Pathological, Comb) {
  std::vector<bw::core::Clipper2Polygon> polygons;

  polygons.push_back(
      {false, ~0u, {{0, 0}, {100, 0}, {100, 20}, {0, 20}}});

  for (int i = 0; i < 10; ++i) {
    int x = 5 + i * 9;

    polygons.push_back(
        {false, ~0u, {{x, 20}, {x + 3, 20}, {x + 3, 40}, {x, 40}}});
  }

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_GE(cycles.size(), 11u);
}

TEST(PSLG_Pathological, HoleTouchesShellAtVertex) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0}, {100, 0}, {100, 100}, {0, 100}}},
          {true,
           ~0u,
           {{0, 0}, {20, 0}, {20, 20}, {0, 20}}}};

  EXPECT_NO_THROW(
      {
        auto pslg = BuildTestPSLG(polygons);
      });
}

TEST(PSLG_Pathological, FigureEightTouch) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0}, {20, 0}, {20, 20}, {0, 20}}},
          {false,
           ~0u,
           {{20, 20}, {40, 20}, {40, 40}, {20, 40}}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 2u);
}

TEST(PSLG_Pathological, Grid20x20) {
  std::vector<bw::core::Clipper2Polygon> polygons;

  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 20; ++x) {
      polygons.push_back(
          {false,
           ~0u,
           {{x * 10, y * 10},
            {(x + 1) * 10, y * 10},
            {(x + 1) * 10, (y + 1) * 10},
            {x * 10, (y + 1) * 10}}});
    }
  }

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 400u);
}

TEST(PSLG_Fuzz, RandomRectangles) {
  for (auto seed : {12345u, 8675309u, 0xC0FFEEu, 0xDEADBEEFu}) {
    std::mt19937 rng(seed);

    for (int iter = 0; iter < 250; ++iter) {
      std::vector<bw::core::Clipper2Polygon> polygons;

      for (int i = 0; i < 50; ++i) {
        int x = rng() % 1000;
        int y = rng() % 1000;

        int w = 10 + rng() % 100;
        int h = 10 + rng() % 100;

        polygons.push_back(
            {false,
             ~0u,
             {{x, y},
              {x + w, y},
              {x + w, y + h},
              {x, y + h}}});
      }

      EXPECT_NO_THROW(
          {
            auto pslg = BuildTestPSLG(polygons);
            auto cycles = ExtractMinimalCycles(pslg);
            auto tree = BuildPolygonHierarchy(pslg, cycles);
            auto faces = BuildFaces(tree, cycles);
          });
    }
  }
}

////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();

  if (gClipperAllocatorsInitialized) {
    Clipper2Lib::WmDestroyAllocators();
  }

  return result;

  /*
  string filename;
  if (argc < 2)
  {
          filename = "../../../../experiments/resources/test-1.yaml";
  }
  else
  {
          filename = argv[1];
  }

  try
  {
          auto world = openWorld(filename);

          Clipper2Lib::WmInitialiseAllocators(4, 16 * 1024 * 1024);

          // Create intermediate polygons

          // Intersect polygons
          // - Check clipper logic for collinear edges
          // - Use clipper intersection function
          // - Just do n^2 test for now, to prove it works conceptually, before doing sweep-lines
          // - Just split vectors and insert in the middle for now

          // Build graph
          // - How to handle polygons entirely within other polygons?
          //   This should produce "sector within sector", with outer sector having a hole
          // Find minimal cycles
          // - Will need to reconstruct the hole information by testing points

          Clipper2Lib::Paths64 input;

          input.push_back({
                  {0,0},
                  {100,0},
                  {100,100},
                  {0,100}
          });

          // polygon B
          input.push_back({
                  {50,50},
                  {150,50},
                  {150,150},
                  {50,150}
          });

          // Hole in A
          input.push_back({
                  {10,10},
                  {10,20},
                  {20,20},
                  {20,10}
          });

          // Clockwise has negative area.  Therefore, if the input cycle is clockwise (hole),
          // we keep the negative one.
          // So: if it's a hole, then it will be clockwise, and we only keep the negative loop
          // If it's not a hole, we want to keep both, because if the polygon is inside another,
          // we will need it both as a hole for the containing polygon, and a polygon in its own right.
          // If it's not inside another, then we remove the clockwise (negative area) loop.
          // Clipper will return correctly-ordered edges to us
          auto graph = expr::BuildPSLG(input);
          auto cycles = expr::ExtractMinimalCycles(graph);
          auto hierarchy = expr::BuildPolygonHierarchy(graph, cycles);

          Clipper2Lib::WmDestroyAllocators();
  }
  catch (std::exception& e)
  {
          cout << e.what() << "\n";
          return 1;
  }

  return 0;
  */
}