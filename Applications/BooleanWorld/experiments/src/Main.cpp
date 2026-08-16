#include <iostream>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <random>
#include <thread>
#include <type_traits>

#include <gtest/gtest.h>

#include <willpower/common/Timer.h>

#include <core/YamlSerializer.h>
#include <core/World.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
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

TEST(WorldSerialization, PathUsesUnknownPrimitiveError) {
  constexpr auto serializedWorld = R"yaml(
world:
  name: path-is-unknown
  description: ""
  minExtent: [-10, -10]
  maxExtent: [10, 10]
  playerStartPosition: [0, 0]
  playerStartAngle: 0
  tiling:
    prefabAreaTilingType: 0
    prefabAreaTileTypes: 0
  primitives:
    - type: Path
)yaml";

  auto serializer = shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(serializedWorld));
  serializer->deserialize();

  bw::core::World world(20.0f, -1.0f);
  auto workData = bw::core::SerializationWorkData{};

  EXPECT_FALSE(world.deserialize(serializer, workData));
  ASSERT_EQ(world.getDeserializationErrors().size(), 1u);
  EXPECT_EQ(world.getDeserializationErrors().front(),
            "No primitive of type 'Path' registered");
}

TEST(ArrangementWorldDataGenerator, PrimitiveConversionProducesNativeContours) {
  bw::core::RectanglePolygon rectangle(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rectangle.setPosition({1.2344f, -2.3456f});
  rectangle.setSize(4.0f, 6.0f);

  auto contours = bw::core::ConvertPrimitiveToContours(rectangle);
  static_assert(std::is_same_v<
                decltype(contours),
                std::vector<bw::core::arr::Contour>>);

  size_t contourIndex = 0;
  for (auto const& complexPolygon : rectangle.getVertices()) {
    for (auto const& polygon : complexPolygon) {
      ASSERT_LT(contourIndex, contours.size());
      ASSERT_EQ(contours[contourIndex].size(), polygon.size());
      for (size_t vertexIndex = 0; vertexIndex < polygon.size(); ++vertexIndex) {
        EXPECT_EQ(
            contours[contourIndex][vertexIndex].x,
            bw::core::arr::ToFixedPointCoordinate(
                polygon[vertexIndex].p.x));
        EXPECT_EQ(
            contours[contourIndex][vertexIndex].y,
            bw::core::arr::ToFixedPointCoordinate(
                polygon[vertexIndex].p.y));
      }
      ++contourIndex;
    }
  }
  EXPECT_EQ(contourIndex, contours.size());
}

TEST(ArrangementWorldDataGenerator, PublishedWorldUsesArrangementContract) {
  auto world = createWorld(8192, 512);

  auto room = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  room->setPosition({0, 0});
  room->setSize(20, 20);
  world->addPrimitive(room);
  world->update(0, {{0, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});

  auto published = world->getWorldData({0, 0}, 0);
  bw::core::ArrangementWorldDataGenerator arrangementGenerator;
  arrangementGenerator.generate(world.get());
  auto arrangement = arrangementGenerator.getWorldData();

  EXPECT_FALSE(published->getTriangles().empty());
  ASSERT_NE(arrangement, nullptr);
  EXPECT_TRUE(std::any_of(
      arrangement->faces.begin(), arrangement->faces.end(),
      [](bw::core::arr::ArrangementFace const& face) {
        return face.solid;
      }));
  EXPECT_EQ(
      dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
          world->getWorldDataGenerator()) != nullptr,
      true);
}

TEST(ArrangementWorldDataGenerator, SelectsMultipleLayersBeforeGlobalPriorityFold) {
  auto world = createWorld(8192, 512);
  auto base = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  base->setSize(100, 100);
  base->setLayer(0);
  base->setPriority(10);
  world->addPrimitive(base);
  auto cut = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Difference,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  cut->setSize(50, 50);
  cut->setLayer(1);
  cut->setPriority(20);
  world->addPrimitive(cut);
  auto shared = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  shared->setPosition({200, 0});
  shared->setSize(20, 20);
  shared->setLayer(BW_LAYER_ALL);
  world->addPrimitive(shared);

  auto solidAt = [&](bw::core::LayerSelection const& selection,
                     bw::core::arr::FixedPointVertex point) {
    bw::core::ArrangementWorldDataGenerator generator;
    generator.setLayerSelection(selection);
    generator.generate(world.get());
    auto arrangement = generator.getWorldData();
    return std::any_of(
        arrangement->faces.begin(), arrangement->faces.end(),
        [&](bw::core::arr::ArrangementFace const& face) {
          return face.solid && bw::core::arr::PointInFace(point, face, *arrangement);
        });
  };

  EXPECT_TRUE(solidAt(bw::core::SelectLayer(0), {0, 0}));
  EXPECT_FALSE(solidAt(bw::core::SelectLayer(1), {0, 0}));
  auto both = bw::core::SelectLayer(0) | bw::core::SelectLayer(1);
  EXPECT_FALSE(solidAt(both, {0, 0}));
  EXPECT_TRUE(solidAt(bw::core::SelectLayer(1), {200000, 0}));
}

TEST(Generation, LayerSelectionChangeBypassesVisibleCommitGate) {
  auto world = createWorld(8192, 512);
  auto base = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  base->setSize(20, 20);
  base->setLayer(0);
  world->addPrimitive(base);
  auto replacement = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  replacement->setPosition({200, 0});
  replacement->setSize(20, 20);
  replacement->setLayer(1);
  replacement->setAnimationValues(
      bw::core::VertexTransformer::Key::Angle,
      {{0.0f, 0.0f}, {1.0f, 90.0f}});
  world->addPrimitive(replacement);
  world->update(
      0, {{200, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});
  auto generator = static_cast<bw::core::DynamicWorldDataGenerator*>(
      world->getWorldDataGenerator());
  generator->setAlwaysUpdateVertices(true);
  auto initial = world->getWorldData({200, 0}, 0);
  ASSERT_NE(initial, nullptr);
  EXPECT_FALSE(replacement->isStatic());

  generator->setLayerSelection(bw::core::SelectLayer(1));
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  auto switched = initial;
  while (switched == initial && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    switched = world->getWorldData({200, 0}, 0);
  }

  ASSERT_NE(switched, initial);
  EXPECT_FALSE(switched->getTriangles().empty());
  EXPECT_TRUE(std::any_of(
      switched->getArrangement().faces.begin(),
      switched->getArrangement().faces.end(),
      [&](bw::core::arr::ArrangementFace const& face) {
        return face.solid && face.primitiveIndex == replacement->getId();
      }));
}

TEST(GeometryComparison, SamplesAllStrategiesAndReportsEquivalentWorld) {
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
      8u);
  EXPECT_DOUBLE_EQ(report.oldSolidArea, 400.0);
  EXPECT_DOUBLE_EQ(report.newSolidArea, 400.0);
  EXPECT_TRUE(report.matches());
}

TEST(GeometryComparison, RepositoryWorldsMatchExceptKnownBadRepros) {
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

TEST(Generation, SynchronousGeneratorPublishesArrangementData) {
  bw::core::World world(8192, 512);
  auto room = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  room->setSize(20, 20);
  world.addPrimitive(room);

  auto snapshot = world.getWorldData({0, 0}, 0);

  ASSERT_NE(snapshot, nullptr);
  EXPECT_FALSE(snapshot->getTriangles().empty());
  EXPECT_TRUE(std::any_of(
      snapshot->getArrangement().faces.begin(),
      snapshot->getArrangement().faces.end(),
      [](bw::core::arr::ArrangementFace const& face) { return face.solid; }));
}

TEST(Generation, PublishedSnapshotRemainsCoherentAfterNextGeneration) {
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
  ASSERT_FALSE(firstGeneration->getTriangles().empty());
  auto firstVertex = firstGeneration->getArrangement().vertices.front();

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
  EXPECT_TRUE(secondGeneration->getTriangles().empty());
  EXPECT_FALSE(firstGeneration->getTriangles().empty());
  EXPECT_EQ(firstGeneration->getArrangement().vertices.front(), firstVertex);
}

TEST(Generation, GeometryIsIndependentOfViewerPosition) {
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

  EXPECT_TRUE(generatedAtRoom->getTriangles().empty());
  EXPECT_TRUE(generatedFarAway->getTriangles().empty());
}

////////////////////////////////////////////////////////////////
// Geometry tests

using namespace bw::core::arr;

static_assert(
    std::is_same_v<decltype(bw::core::arr::FixedPointVertex::x), int64_t>);
static_assert(std::is_same_v<
              bw::core::arr::Contour::value_type,
              bw::core::arr::FixedPointVertex>);
static_assert(bw::core::arr::FixedPointUnitsPerWorldUnit == 1000);

TEST(NativeArrangement, ClassifiesGeometryWithoutIntrinsicContourRoles) {
  using namespace bw::core::arr;
  ArrangementPrimitive primitive{
      {{{0, 0}, {30, 0}, {30, 30}, {0, 30}},
       {{10, 10}, {20, 10}, {20, 20}, {10, 20}}},
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      0,
      42};

  ArrangementResultPtr arrangement = BuildArrangement({primitive});
  auto shell = std::find_if(
      arrangement->faces.begin(), arrangement->faces.end(),
      [&](ArrangementFace const& face) {
        return PointInFace({5, 5}, face, *arrangement);
      });
  auto hole = std::find_if(
      arrangement->faces.begin(), arrangement->faces.end(),
      [&](ArrangementFace const& face) {
        return PointInFace({15, 15}, face, *arrangement);
      });

  ASSERT_NE(shell, arrangement->faces.end());
  ASSERT_NE(hole, arrangement->faces.end());
  EXPECT_TRUE(shell->membership.contains(0));
  EXPECT_TRUE(shell->solid);
  EXPECT_FALSE(hole->membership.contains(0));
  EXPECT_FALSE(hole->solid);
  EXPECT_EQ(shell->innerBoundaries.size(), 1u);
}

static PSLG BuildTestPSLG(std::vector<Contour> contours) {
  std::vector<ContourInput> inputs;
  inputs.reserve(contours.size());
  for (auto& contour : contours) {
    for (auto& point : contour) {
      point.x *= FixedPointUnitsPerWorldUnit;
      point.y *= FixedPointUnitsPerWorldUnit;
    }
    inputs.push_back({std::move(contour)});
  }
  return BuildPSLG(inputs);
}

static PSLG BuildNativeTestPSLG(std::vector<Contour> const& contours) {
  std::vector<ContourInput> inputs;
  inputs.reserve(contours.size());
  for (auto const& contour : contours) {
    inputs.push_back({contour});
  }
  return BuildPSLG(inputs);
}

static int VertexDegree(PSLG const& graph, bw::core::arr::FixedPointVertex const& vertex) {
  auto vertexIt = std::find(graph.vs.begin(), graph.vs.end(), vertex);
  if (vertexIt == graph.vs.end()) {
    return 0;
  }

  auto vertexIndex = (int)std::distance(graph.vs.begin(), vertexIt);
  return (int)std::count_if(graph.es.begin(), graph.es.end(), [&](bw::core::arr::Edge const& edge) {
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
    std::vector<PolygonNode> const& nodes) {
  int count = 0;

  for (auto& n : nodes) {
    if (n.parent == -1)
      ++count;
  }

  return count;
}

static void RunBasicTest(
    std::vector<Contour> const& polygons,
    Expected const& expected) {
  // Create graph
  bw::core::arr::PSLG pslg = BuildTestPSLG(polygons);

  // Get cycles
  auto cycles = bw::core::arr::ExtractMinimalCycles(pslg);

  // Set faces as being either filled or not

  // Build hierarchy.
  auto hierarchy = bw::core::arr::BuildPolygonHierarchy(pslg, cycles);

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

using namespace bw::core::arr;

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
    bw::core::arr::FixedPointVertex const& point) {
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

TEST(World, StepThresholdDefaultsToInfinity) {
  bw::core::World world;
  EXPECT_TRUE(std::isinf(world.getStepThreshold()));
  world.setStepThreshold(3.5f);
  EXPECT_EQ(world.getStepThreshold(), 3.5f);
}

TEST(World, BakesArrangementFacesWithNestedHoles) {
  auto world = createWorld(BW_WORLD_SIZE, 100.0f);
  auto outer = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      1.0f);
  outer->setSize(100.0f, 100.0f);
  auto hole = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Difference,
      bw::core::Primitive::FillRule::EvenOdd,
      1.0f);
  hole->setSize(50.0f, 50.0f);
  hole->setPriority(1);
  world->addPrimitive(outer);
  world->addPrimitive(hole);
  world->update(0, {{0, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});

  auto baked =
      world->createMeshPrimitive(vector<bw::core::Primitive*>{outer, hole});

  ASSERT_NE(baked, nullptr);
  world->addPrimitive(baked);
  world->update(0, {{0, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});
  ASSERT_EQ(baked->getVertices().size(), 1u);
  EXPECT_EQ(baked->getVertices()[0].size(), 2u);
}

TEST(World, BakesFoldClippedByHighestPriorityIntersection) {
  auto world = createWorld(BW_WORLD_SIZE, 100.0f);
  auto shape = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      1.0f);
  shape->setSize(100.0f, 100.0f);
  auto cell = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Intersection,
      bw::core::Primitive::FillRule::EvenOdd,
      1.0f);
  cell->setPosition({0.0f, 0.0f});
  cell->setSize(50.0f, 50.0f);
  cell->setPriority(BW_PRIORITY_MAX_VALUE);
  world->addPrimitive(shape);
  world->addPrimitive(cell);
  world->update(0, {{0, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});

  auto baked =
      world->createMeshPrimitive(vector<bw::core::Primitive*>{shape, cell});

  ASSERT_NE(baked, nullptr);
  world->addPrimitive(baked);
  world->update(0, {{0, 0}, 0, 1, 60, 256, false, false, 0}, {100, 100});
  auto bounds = baked->getBounds();
  wp::Vector2 minExtent, maxExtent;
  bounds.getExtents(minExtent, maxExtent);
  EXPECT_NEAR(minExtent.x, -25.0f, 0.001f);
  EXPECT_NEAR(maxExtent.x, 25.0f, 0.001f);
}

TEST(ArrangementWorldData, IndexesFacesAndBlockingWalls) {
  bw::core::PrimitivePropertySet lowerProperties{};
  lowerProperties.floorZ = 0;
  lowerProperties.ceilingZ = 10;
  bw::core::PrimitivePropertySet higherProperties{};
  higherProperties.floorZ = 2;
  higherProperties.ceilingZ = 10;
  auto arrangement = BuildArrangement(
      {{{{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        0,
        42,
        lowerProperties},
       {{{{10000, 0}, {20000, 0}, {20000, 10000}, {10000, 10000}}},
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        1,
        77,
        higherProperties}});

  bw::core::ArrangementWorldData blocking(
      arrangement, wp::BoundingBox(-10, -10, 40, 30), 5, 1);
  EXPECT_EQ(blocking.getContainingPrimitiveIndex({5, 5}), 42u);
  EXPECT_EQ(blocking.getContainingPrimitiveIndex({15, 5}), 77u);
  EXPECT_EQ(blocking.getFloorHeight({5, 5}), 0);
  EXPECT_EQ(blocking.getFloorHeight({15, 5}), 2);
  EXPECT_GE(blocking.circleIntersectsWall({10, 5}, 0.1f), 0);

  bw::core::ArrangementWorldData walkable(
      arrangement,
      wp::BoundingBox(-10, -10, 40, 30),
      5,
      std::numeric_limits<float>::infinity());
  EXPECT_EQ(walkable.circleIntersectsWall({10, 5}, 0.1f), -1);
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

TEST(ArrangementOutput, OwnerSelectionUsesPriorityAndStableTies) {
  auto build = [](uint8_t secondPriority) {
    return BuildArrangement(
        {{{{{0, 0}, {20, 0}, {20, 20}, {0, 20}}},
          bw::core::Primitive::Operation::Union,
          bw::core::Primitive::FillRule::NonZero,
          10,
          42},
         {{{{10, 0}, {30, 0}, {30, 20}, {10, 20}}},
          bw::core::Primitive::Operation::Union,
          bw::core::Primitive::FillRule::NonZero,
          secondPriority,
          77}});
  };

  auto higherPriorityArrangement = build(20);
  auto higherPriority =
      FindFaceAt(higherPriorityArrangement, {15, 10});
  ASSERT_NE(higherPriority, nullptr);
  EXPECT_EQ(higherPriority->primitiveIndex, 77u);

  auto stableTieArrangement = build(10);
  auto stableTie = FindFaceAt(stableTieArrangement, {15, 10});
  ASSERT_NE(stableTie, nullptr);
  EXPECT_EQ(stableTie->primitiveIndex, 42u);
}

TEST(PSLG, SingleRectangle) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {10, 0},
           {10, 10},
           {0, 10}}};

  RunBasicTest(
      polygons,
      {4,
       4,
       1,
       1});
}

TEST(PSLG, IntersectionsAreSnapRoundedToFixedPointGrid) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {10, 0}, {10, -5}, {0, -5}},
          {{1, -1}, {2, 2}, {3, -1}}};

  auto graph = BuildTestPSLG(polygons);

  EXPECT_NE(
      std::find(graph.vs.begin(), graph.vs.end(), bw::core::arr::FixedPointVertex{1333, 0}),
      graph.vs.end());
}

TEST(PSLG, SnapRoundingCreatesIncidenceOnPreviouslyUnrelatedEdge) {
  std::vector<Contour> polygons =
      {
          {{-10, 1}, {10, 0}, {10, 10}},
          {{-10, 0}, {10, 1}, {-10, -10}},
          {{-1, 1}, {1, 1}, {1, 2}, {-1, 2}}};

  auto graph = BuildNativeTestPSLG(polygons);

  EXPECT_EQ(VertexDegree(graph, {0, 1}), 6);
}

TEST(PSLG, CollinearOverlapsProduceOneConsistentEdgeChain) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
          {{5, 0}, {15, 0}, {15, -10}, {5, -10}}};

  auto graph = BuildNativeTestPSLG(polygons);

  EXPECT_EQ(graph.vs.size(), 8u);
  EXPECT_EQ(graph.es.size(), 9u);
  EXPECT_EQ(VertexDegree(graph, {5, 0}), 3);
  EXPECT_EQ(VertexDegree(graph, {10, 0}), 3);
}

TEST(PSLG, GridQuantumGeometrySurvivesAtEveryWorldExtent) {
  constexpr int64_t extent = 4'096'000;
  std::vector<Contour> polygons =
      {
          {{-extent, -extent}, {-extent + 1, -extent}, {-extent + 1, -extent + 1}, {-extent, -extent + 1}},
          {{extent - 1, -extent}, {extent, -extent}, {extent, -extent + 1}, {extent - 1, -extent + 1}},
          {{-extent, extent - 1}, {-extent + 1, extent - 1}, {-extent + 1, extent}, {-extent, extent}},
          {{extent - 1, extent - 1}, {extent, extent - 1}, {extent, extent}, {extent - 1, extent}}};

  auto graph = BuildNativeTestPSLG(polygons);
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
  Contour contour =
      {{0, 0}, {2'000, 2'000}, {0, 2'000}, {2'000, 0}};
  auto graph = BuildNativeTestPSLG({contour});
  auto cycles = ExtractMinimalCycles(graph);
  auto arrangement = BuildArrangement(
      {{{contour},
        bw::core::Primitive::Operation::Union,
        fillRule,
        0,
        0}});

  EXPECT_EQ(graph.vs.size(), 5u);
  EXPECT_EQ(graph.es.size(), 6u);
  EXPECT_EQ(cycles.size(), 2u);
  EXPECT_EQ(arrangement->vertices.size(), 5u);
  EXPECT_EQ(arrangement->edges.size(), 6u);
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
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {10, 0},
           {10, 10},
           {0, 10}},
          {{20, 0},
           {30, 0},
           {30, 10},
           {20, 10}}};

  RunBasicTest(
      polygons,
      {8,
       8,
       2,
       2});
}

TEST(PSLG, PolygonWithHole) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {20, 0},
           {20, 20},
           {0, 20}},
          {{5, 5},
           {5, 15},
           {15, 15},
           {15, 5}}};

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
}

TEST(PSLG, PolygonWithIsland) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {20, 0},
           {20, 20},
           {0, 20}},
          {{5, 5},
           {15, 5},
           {15, 15},
           {5, 15}}};

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
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {20, 0},
           {20, 20},
           {0, 20}},
          {{5, 5},
           {15, 5},
           {15, 15},
           {5, 15}},
          {{8, 8},
           {8, 12},
           {12, 12},
           {12, 8}}};

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
      4u);

  ASSERT_EQ(
      hierarchy.size(),
      4u);
}

TEST(PSLG, TwoPolygonsWithHole) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {100, 0},
           {100, 100},
           {0, 100}},
          {{10, 10},
           {10, 90},
           {90, 90},
           {90, 10}},
          {{50, 50},
           {150, 50},
           {150, 150},
           {50, 150}},
          {{60, 60},
           {60, 140},
           {140, 140},
           {140, 60}}};

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
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {30, 0},
           {30, 30},
           {0, 30}},
          {{5, 5},
           {5, 25},
           {25, 25},
           {25, 5}},
          {{10, 10},
           {20, 10},
           {20, 20},
           {10, 20}}};

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
      4u);

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

  ASSERT_EQ(depths.size(), 4u);
  EXPECT_EQ(depths[0], 0);
  EXPECT_EQ(depths[1], 1);
  EXPECT_EQ(depths[2], 2);
  EXPECT_EQ(depths[3], 2);
}

TEST(PSLG, TwoHoles) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {40, 0},
           {40, 40},
           {0, 40}},

          {{5, 5},
           {5, 15},
           {15, 15},
           {15, 5}},

          {{25, 5},
           {35, 5},
           {35, 15},
           {25, 15}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  auto faces = BuildFaces(hierarchy, cycles);

  EXPECT_EQ(hierarchy.size(), 5u);
}

TEST(PSLG, SharedEdge) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {10, 0},
           {10, 10},
           {0, 10}},

          {{10, 0},
           {20, 0},
           {20, 10},
           {10, 10}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      2u);
}

TEST(PSLG, TouchingVertex) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {10, 0},
           {10, 10},
           {0, 10}},

          {{10, 10},
           {20, 10},
           {15, 20}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      2u);
}

TEST(PSLG, FiveLevelHierarchy) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
          {{10, 10}, {10, 90}, {90, 90}, {90, 10}},
          {{20, 20}, {80, 20}, {80, 80}, {20, 80}},
          {{30, 30}, {30, 70}, {70, 70}, {70, 30}},
          {{40, 40}, {60, 40}, {60, 60}, {40, 60}}};

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
      6u);

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

  ASSERT_EQ(depths.size(), 6u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(
        depths[i],
        i);
  }
  EXPECT_EQ(depths[5], 4);
}

TEST(PSLG, Grid3x3) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {30, 0},
           {30, 30},
           {0, 30}},

          {{10, 0},
           {20, 0},
           {20, 30},
           {10, 30}},

          {{0, 10},
           {30, 10},
           {30, 20},
           {0, 20}}};

  PSLG pslg =
      BuildTestPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      9u);
}

TEST(PSLG_Pathological, FourWayVertex) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
          {{10, 0}, {20, 0}, {20, 10}, {10, 10}},
          {{0, 10}, {10, 10}, {10, 20}, {0, 20}},
          {{10, 10}, {20, 10}, {20, 20}, {10, 20}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, TJunction) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {30, 0}, {30, 20}, {0, 20}},
          {{10, 0}, {20, 0}, {20, 10}, {10, 10}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 2u);
}

TEST(Hierarchy_Pathological, EightChildren) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
          {{20, 20}, {40, 20}, {40, 40}, {20, 40}},
          {{45, 20}, {55, 20}, {55, 40}, {45, 40}},
          {{60, 20}, {80, 20}, {80, 40}, {60, 40}},
          {{20, 45}, {40, 45}, {40, 55}, {20, 55}},
          {{60, 45}, {80, 45}, {80, 55}, {60, 55}},
          {{20, 60}, {40, 60}, {40, 80}, {20, 80}},
          {{45, 60}, {55, 60}, {55, 80}, {45, 80}},
          {{60, 60}, {80, 60}, {80, 80}, {60, 80}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);
  auto tree = BuildPolygonHierarchy(pslg, cycles);
  auto faces = BuildFaces(tree, cycles);

  EXPECT_GE(tree.size(), 9u);
}

TEST(Hierarchy_Pathological, DeepNesting) {
  std::vector<Contour> polygons;

  for (int i = 0; i < 5; ++i) {
    int s = i * 10;

    Contour contour;

    if (i % 2 == 1) {
      contour = {
          {s, s},
          {s, 100 - s},
          {100 - s, 100 - s},
          {100 - s, s}};
    } else {
      contour = {
          {s, s},
          {100 - s, s},
          {100 - s, 100 - s},
          {s, 100 - s}};
    }

    polygons.push_back(std::move(contour));
  }

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);
  auto tree = BuildPolygonHierarchy(pslg, cycles);
  auto faces = BuildFaces(tree, cycles);

  ASSERT_EQ(tree.size(), 6u);

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
  std::vector<Contour> polygons =
      {
          {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
          {{10, 0}, {20, 0}, {20, 10}, {10, 10}},
          {{20, 0}, {30, 0}, {30, 10}, {20, 10}},
          {{30, 0}, {40, 0}, {40, 10}, {30, 10}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, Pinwheel) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {50, 50}, {0, 100}},
          {{0, 100}, {50, 50}, {100, 100}},
          {{100, 100}, {50, 50}, {100, 0}},
          {{100, 0}, {50, 50}, {0, 0}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, ThinSliver) {
  std::vector<Contour> polygons =
      {
          {{0, 0},
           {1000000, 0},
           {1000000, 1},
           {0, 1}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 1u);
}

TEST(PSLG_Pathological, Comb) {
  std::vector<Contour> polygons;

  polygons.push_back(
      {{0, 0}, {100, 0}, {100, 20}, {0, 20}});

  for (int i = 0; i < 10; ++i) {
    int x = 5 + i * 9;

    polygons.push_back(
        {{x, 20}, {x + 3, 20}, {x + 3, 40}, {x, 40}});
  }

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_GE(cycles.size(), 11u);
}

TEST(PSLG_Pathological, HoleTouchesShellAtVertex) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
          {{0, 0}, {20, 0}, {20, 20}, {0, 20}}};

  EXPECT_NO_THROW(
      {
        auto pslg = BuildTestPSLG(polygons);
      });
}

TEST(PSLG_Pathological, FigureEightTouch) {
  std::vector<Contour> polygons =
      {
          {{0, 0}, {20, 0}, {20, 20}, {0, 20}},
          {{20, 20}, {40, 20}, {40, 40}, {20, 40}}};

  auto pslg = BuildTestPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 2u);
}

TEST(PSLG_Pathological, Grid20x20) {
  std::vector<Contour> polygons;

  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 20; ++x) {
      polygons.push_back(
          {{x * 10, y * 10},
           {(x + 1) * 10, y * 10},
           {(x + 1) * 10, (y + 1) * 10},
           {x * 10, (y + 1) * 10}});
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
      std::vector<Contour> polygons;

      for (int i = 0; i < 50; ++i) {
        int x = rng() % 1000;
        int y = rng() % 1000;

        int w = 10 + rng() % 100;
        int h = 10 + rng() % 100;

        polygons.push_back(
            {{x, y},
             {x + w, y},
             {x + w, y + h},
             {x, y + h}});
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
  return RUN_ALL_TESTS();
}
