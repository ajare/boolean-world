#include <iostream>
#include <filesystem>
#include <random>

#include <gtest/gtest.h>

#include <willpower/common/Timer.h>

#include <core/YamlSerializer.h>
#include <core/World.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/Clipper2Polygon.h>
#include <core/Arrangement.h>

using namespace std;

shared_ptr<bw::core::World> createWorld(float size, float gridSize) {
  auto world = make_shared<bw::core::World>(size, gridSize);

  auto genFn = [world](wp::Vector2 offset, int dimX, int dimY, float cellSize) {
    auto wdg = new bw::core::DynamicWorldDataGenerator(world.get());

    wdg->setBroadPhaseCulling(bw::core::WorldDataGenerator::BroadPhaseCulling::None);
    wdg->setNarrowPhaseCulling(bw::core::WorldDataGenerator::NarrowPhaseCulling::None);

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

////////////////////////////////////////////////////////////////
// Geometry tests

using namespace Clipper2Lib;
using namespace expr;

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
  std::mt19937 rng(12345);

  for (int iter = 0; iter < 1000; ++iter) {
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

////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();

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