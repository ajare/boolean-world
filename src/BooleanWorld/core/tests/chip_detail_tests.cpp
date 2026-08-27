#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/Chips.h>
#include <core/PrimitivePropertySet.h>

namespace {
using bw::core::ArrangementWorldData;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;
using bw::core::arr::DetailGeometry;
using bw::core::arr::DetailSurfaceKind;
using bw::core::arr::DetailTriangle;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(float a, float b, float tolerance = 0.01f) {
  return std::abs(a - b) <= tolerance;
}

// Fixed-point units are 1000 per world unit (Arrangement.h).
constexpr int64_t U = 1000;

// The compiled-in sizes every Chip is cut at for now (#278).
float const chipDepth = bw::core::arr::DefaultChipSizes().depth;
float const chipReach = bw::core::arr::DefaultChipSizes().reach;

Contour rectContour(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {{x0 * U, y0 * U}, {x1 * U, y0 * U}, {x1 * U, y1 * U}, {x0 * U, y1 * U}};
}

PrimitivePropertySet propertiesWithHeights(float floorZ, float ceilingZ) {
  PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  return properties;
}

// A 100x100 ground slab at floorZ 0 with a 40x40 platform raised to
// platformFloorZ standing on it. The platform's four sides are the only
// FloorStep walls in the fixture; the slab's four sides are Borders.
//
// `platformVisibleOverrides` is indexed by the platform contour's edges:
// 0 is the south side from (-20, -20) to (20, -20), then anticlockwise.
std::vector<ArrangementPrimitive> slabAndPlatform(
    float platformFloorZ,
    std::vector<std::optional<bool>> const& platformVisibleOverrides = {},
    bool withDistantSlab = false) {
  std::vector<ArrangementPrimitive> primitives;
  if (withDistantSlab) {
    // Deliberately first, so every vertex and edge the fixture proper
    // produces is renumbered by its presence.
    primitives.push_back(
        {{rectContour(200, 200, 240, 240)},
         Primitive::Operation::Union,
         Primitive::FillRule::EvenOdd,
         0,
         uint32_t(primitives.size()),
         propertiesWithHeights(0.0f, 48.0f)});
  }
  primitives.push_back(
      {{rectContour(-50, -50, 50, 50)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       1,
       uint32_t(primitives.size()),
       propertiesWithHeights(0.0f, 48.0f)});
  primitives.push_back(
      {{rectContour(-20, -20, 20, 20)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       2,
       uint32_t(primitives.size()),
       propertiesWithHeights(platformFloorZ, 48.0f),
       {},
       platformVisibleOverrides.empty()
           ? std::vector<std::vector<std::optional<bool>>>{}
           : std::vector<std::vector<std::optional<bool>>>{
                 platformVisibleOverrides}});
  return primitives;
}

ArrangementWorldData snapshotOf(
    std::vector<ArrangementPrimitive> const& primitives) {
  return ArrangementWorldData(
      bw::core::arr::BuildArrangement(primitives),
      wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}),
      64.0f,
      8.0f);
}

// The index of the wall of `kind` whose edge's midpoint is at `midpoint`.
uint32_t findWall(
    ArrangementWorldData const& snapshot,
    ArrangementWallKind kind,
    wp::Vector2 const& midpoint) {
  auto const& arrangement = snapshot.getArrangement();
  auto const& walls = snapshot.getWalls();
  for (uint32_t i = 0; i < uint32_t(walls.size()); ++i) {
    if (walls[i].kind != kind) {
      continue;
    }
    auto orientation =
        bw::core::arr::OrientArrangementWall(arrangement, walls[i]);
    auto centre = (orientation.v0 + orientation.v1) * 0.5f;
    if (near(centre.x, midpoint.x) && near(centre.y, midpoint.y)) {
      return i;
    }
  }
  return ~0u;
}

uint32_t countWalls(
    ArrangementWorldData const& snapshot,
    ArrangementWallKind kind) {
  uint32_t count = 0;
  for (auto const& wall : snapshot.getWalls()) {
    if (wall.kind == kind) {
      ++count;
    }
  }
  return count;
}

float triangleArea2d(
    std::array<float, 3> const& a,
    std::array<float, 3> const& b,
    std::array<float, 3> const& c) {
  return std::abs(
             (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) *
      0.5f;
}

bool hasVertexAt(
    std::span<DetailTriangle const> triangles,
    float x,
    float y,
    float z) {
  for (auto const& triangle : triangles) {
    for (auto const& vertex : triangle.v) {
      if (near(vertex.position[0], x) && near(vertex.position[1], y) &&
          near(vertex.position[2], z)) {
        return true;
      }
    }
  }
  return false;
}

// Every replacement position in the channel, sorted, so two generations can
// be compared for geometric identity without depending on face, edge or wall
// numbering.
std::vector<std::array<float, 3>> sortedPositions(
    DetailGeometry const& detail) {
  std::vector<std::array<float, 3>> positions;
  for (auto const& triangle : detail.getTriangles()) {
    for (auto const& vertex : triangle.v) {
      positions.push_back(vertex.position);
    }
  }
  std::sort(positions.begin(), positions.end());
  return positions;
}

// 1. Exactly one Chip per FloorStep, none on a Border.
void everyFloorStepTopArrisCarriesOneChipAndNoBorderDoes() {
  auto snapshot = snapshotOf(slabAndPlatform(12.0f));
  auto const& detail = snapshot.getDetail();

  auto floorSteps = countWalls(snapshot, ArrangementWallKind::FloorStep);
  require(floorSteps == 4,
          "the slab/platform fixture did not produce four FloorStep walls");
  require(detail.getChipCount() == floorSteps,
          "the number of Chips did not match the number of eligible Arrises");
  require(countWalls(snapshot, ArrangementWallKind::CeilingStep) == 0,
          "the fixture produced a CeilingStep it was not meant to");

  auto const& walls = snapshot.getWalls();
  for (uint32_t i = 0; i < uint32_t(walls.size()); ++i) {
    auto suppressed = detail.isSuppressed(DetailSurfaceKind::Wall, i);
    if (walls[i].kind == ArrangementWallKind::FloorStep) {
      require(suppressed, "a FloorStep wall's top Arris carried no Chip");
    } else {
      require(!suppressed,
              "a wall with no convex Arris - a Border - carried a Chip");
    }
  }
}

// 2. The chamfer's shape: 45 degrees, deepest at the centre, tapering to
//    nothing at both ends, and closing on itself without end caps.
void theChamferIsATaperedFortyFiveDegreeFacet() {
  auto snapshot = snapshotOf(slabAndPlatform(12.0f));
  auto const& detail = snapshot.getDetail();

  // The platform's south side: the Arris runs from (-20, -20) to (20, -20)
  // at z = 12, and the platform - the higher face - lies to the north.
  auto wallIndex =
      findWall(snapshot, ArrangementWallKind::FloorStep, {0.0f, -20.0f});
  require(wallIndex != ~0u, "the platform's south FloorStep was not found");
  auto const& wall = snapshot.getWalls()[wallIndex];
  require(near(wall.minZ, 0.0f) && near(wall.maxZ, 12.0f),
          "the platform's south FloorStep did not span the expected heights");

  auto replacements =
      detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex);
  require(!replacements.empty(),
          "a chipped wall published no replacement geometry");

  // Deepest at the Arris's centre: one point `chipDepth` into the platform
  // floor, and one the same distance down the wall - the 45 degree bevel.
  require(hasVertexAt(replacements, 0.0f, -20.0f + chipDepth, 12.0f),
          "the Chip did not bite chipDepth into the horizontal face at the Arris's centre");
  require(hasVertexAt(replacements, 0.0f, -20.0f, 12.0f - chipDepth),
          "the Chip did not bite chipDepth down the wall at the Arris's centre");

  // Tapering to nothing at both ends: the facet meets the Arris again a half
  // reach either side of centre, at full height and with no bite at all.
  require(hasVertexAt(replacements, -chipReach * 0.5f, -20.0f, 12.0f) &&
              hasVertexAt(replacements, chipReach * 0.5f, -20.0f, 12.0f),
          "the Chip did not taper back to the Arris at both ends");

  // The facet is the part standing off the wall plane. Two triangles, both
  // sharing the deepest cross-section - which is what lets the Chip close on
  // itself rather than needing an end cap.
  uint32_t facetTriangles = 0;
  float wallPlaneArea = 0.0f;
  for (auto const& triangle : replacements) {
    auto offFacet = std::any_of(
        triangle.v.begin(), triangle.v.end(), [](auto const& vertex) {
          return !near(vertex.position[1], -20.0f);
        });
    if (offFacet) {
      ++facetTriangles;
      require(
          std::any_of(
              triangle.v.begin(), triangle.v.end(),
              [](auto const& vertex) {
                return near(vertex.position[0], 0.0f) &&
                    near(vertex.position[1], -20.0f) &&
                    near(vertex.position[2], 12.0f - chipDepth);
              }),
          "a chamfer triangle did not meet the Chip's deepest cross-section");
      continue;
    }
    // Wall-plane geometry measures in (distance along, height).
    std::array<float, 3> flattened[3];
    for (int i = 0; i < 3; ++i) {
      flattened[i] = {
          triangle.v[i].position[0], triangle.v[i].position[2], 0.0f};
    }
    wallPlaneArea +=
        triangleArea2d(flattened[0], flattened[1], flattened[2]);
  }
  require(facetTriangles == 2,
          "the chamfer was not the two flat triangles a tapered 45 degree "
          "chamfer cuts");

  // What is left of the wall is its quad less the Chip's triangular notch -
  // exactly, so the remainder leaves no seam and no overlap.
  auto expectedWallArea = 40.0f * 12.0f - chipReach * chipDepth * 0.5f;
  require(near(wallPlaneArea, expectedWallArea, 0.05f),
          "the chipped wall's remaining area was not its quad less the Chip's notch");
}

// 3. The horizontal face is rebuilt with the footprint subtracted, and only
//    its floor side is suppressed.
void theFloorSideIsRebuiltWithTheFootprintSubtracted() {
  auto snapshot = snapshotOf(slabAndPlatform(12.0f));
  auto const& arrangement = snapshot.getArrangement();
  auto const& detail = snapshot.getDetail();

  auto platformFace = snapshot.getContainingFaceIndex({0.0f, 0.0f});
  require(platformFace != ~0u, "the platform face was not found");
  require(near(arrangement.palette[arrangement.faces[platformFace].paletteIndex]
                   .floorZ,
               12.0f),
          "the located face was not the raised platform");

  require(detail.isSuppressed(DetailSurfaceKind::FloorOfFace, platformFace),
          "the bitten face's floor side was not suppressed");
  require(!detail.isSuppressed(DetailSurfaceKind::CeilingOfFace, platformFace),
          "suppressing the floor side also suppressed the ceiling overhead");

  auto slabFace = snapshot.getContainingFaceIndex({0.0f, 35.0f});
  require(slabFace != ~0u && slabFace != platformFace,
          "the surrounding slab face was not found");
  require(!detail.isSuppressed(DetailSurfaceKind::FloorOfFace, slabFace),
          "a face no Chip bit into had its floor suppressed");

  float rebuiltArea = 0.0f;
  for (auto const& triangle :
       detail.replacementsFor(DetailSurfaceKind::FloorOfFace, platformFace)) {
    for (auto const& vertex : triangle.v) {
      require(near(vertex.position[2], 12.0f),
              "a rebuilt floor triangle left the face's floor plane");
    }
    rebuiltArea +=
        triangleArea2d(triangle.v[0].position, triangle.v[1].position,
                       triangle.v[2].position);
  }
  // The platform is 40x40 and loses one triangular footprint per side.
  auto expectedArea = 40.0f * 40.0f - 4.0f * chipReach * chipDepth * 0.5f;
  require(near(rebuiltArea, expectedArea, 0.1f),
          "the rebuilt face's area was not its boundary less the four Chip footprints");
}

// 3b. Every replacement triangle carries a unit normal facing out of the
//     solid and is wound anticlockwise about it, which is the whole of the
//     channel's orientation contract - the renderer reads the normal for
//     shading and maps the winding straight through.
void replacementTrianglesCarryAnOutwardNormalTheyAreWoundAbout() {
  auto snapshot = snapshotOf(slabAndPlatform(12.0f));
  auto const& detail = snapshot.getDetail();
  auto platformFace = snapshot.getContainingFaceIndex({0.0f, 0.0f});

  for (auto const& triangle : detail.getTriangles()) {
    auto const& a = triangle.v[0].position;
    auto const& b = triangle.v[1].position;
    auto const& c = triangle.v[2].position;
    auto const& n = triangle.v[0].normal;
    require(near(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1.0f),
            "a replacement triangle's normal was not a unit vector");
    for (auto const& vertex : triangle.v) {
      require(vertex.normal == n,
              "a replacement triangle's vertices disagreed on its normal");
    }
    // 2 * signed volume of (a, b, c) against the normal: positive exactly
    // when the winding is anticlockwise seen from the normal's side.
    float u[3]{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float v[3]{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    auto facing = (u[1] * v[2] - u[2] * v[1]) * n[0] +
        (u[2] * v[0] - u[0] * v[2]) * n[1] +
        (u[0] * v[1] - u[1] * v[0]) * n[2];
    require(facing > 0.0f,
            "a replacement triangle was not wound anticlockwise about its normal");
  }

  for (auto const& triangle :
       detail.replacementsFor(DetailSurfaceKind::FloorOfFace, platformFace)) {
    require(near(triangle.v[0].normal[2], 1.0f),
            "a rebuilt floor triangle's normal did not point straight up");
  }
}

// 4. The wall-height clamp: a Chip shrinks to fit rather than eating through
//    the bottom of its own step.
void aChipShrinksToFitItsWallsHeight() {
  auto stepHeight = chipDepth * 0.5f;
  auto snapshot = snapshotOf(slabAndPlatform(stepHeight));
  auto const& detail = snapshot.getDetail();

  auto wallIndex =
      findWall(snapshot, ArrangementWallKind::FloorStep, {0.0f, -20.0f});
  require(wallIndex != ~0u, "the shallow step's south FloorStep was not found");
  auto replacements =
      detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex);
  require(!replacements.empty(), "the shallow step carried no Chip at all");

  for (auto const& triangle : replacements) {
    for (auto const& vertex : triangle.v) {
      require(vertex.position[2] >= -0.01f,
              "a Chip ate through the bottom of its own step");
    }
  }
  // Clamped to the wall's height, and still a 45 degree bevel: the bite into
  // the floor shrinks with the bite down the wall.
  require(hasVertexAt(replacements, 0.0f, -20.0f, 0.0f),
          "the clamped Chip did not reach exactly the bottom of its step");
  require(hasVertexAt(replacements, 0.0f, -20.0f + stepHeight, stepHeight),
          "the clamped Chip's bite into the floor did not shrink to match");
}

// 5. A wall turned off carries no Chip, and its horizontal face is not
//    bitten either.
void anInvisibleWallCarriesNoChip() {
  std::vector<std::optional<bool>> allHidden(4, false);
  auto hidden = snapshotOf(slabAndPlatform(12.0f, allHidden));
  require(hidden.getDetail().getChipCount() == 0,
          "walls whose visibility override is off still carried Chips");
  require(hidden.getDetail().getSuppressed().empty() &&
              hidden.getDetail().getTriangles().empty(),
          "an all-hidden platform still published detail geometry");

  // With only the south side hidden, the other three Arrises still chip, and
  // the face keeps the material the hidden side would have taken.
  std::vector<std::optional<bool>> southHidden(4, std::nullopt);
  southHidden[0] = false;
  auto snapshot = snapshotOf(slabAndPlatform(12.0f, southHidden));
  auto const& detail = snapshot.getDetail();
  require(detail.getChipCount() == 3,
          "hiding one of four walls did not leave exactly three Chips");

  auto platformFace = snapshot.getContainingFaceIndex({0.0f, 0.0f});
  float rebuiltArea = 0.0f;
  for (auto const& triangle :
       detail.replacementsFor(DetailSurfaceKind::FloorOfFace, platformFace)) {
    rebuiltArea +=
        triangleArea2d(triangle.v[0].position, triangle.v[1].position,
                       triangle.v[2].position);
  }
  auto expectedArea = 40.0f * 40.0f - 3.0f * chipReach * chipDepth * 0.5f;
  require(near(rebuiltArea, expectedArea, 0.1f),
          "the hidden wall's Arris still bit into the horizontal face");
}

// 6. Chips are seeded from the Arris's own endpoints, so they stay put.
void chipsStayPutAcrossRegenerationAndUnrelatedEdits() {
  auto first = snapshotOf(slabAndPlatform(12.0f));
  auto again = snapshotOf(slabAndPlatform(12.0f));
  require(sortedPositions(first.getDetail()) ==
              sortedPositions(again.getDetail()),
          "regenerating an unchanged world moved its Chips");

  auto edited = snapshotOf(slabAndPlatform(12.0f, {}, true));
  require(edited.getArrangement().edges.size() >
              first.getArrangement().edges.size(),
          "the unrelated edit did not add arrangement edges (fixture broken)");
  auto before =
      first.getWalls()[findWall(first, ArrangementWallKind::FloorStep,
                                {0.0f, -20.0f})]
          .edge;
  auto after =
      edited.getWalls()[findWall(edited, ArrangementWallKind::FloorStep,
                                 {0.0f, -20.0f})]
          .edge;
  require(before != after,
          "the unrelated edit did not renumber the Arris's edge (fixture broken)");
  require(edited.getDetail().getChipCount() == first.getDetail().getChipCount(),
          "an unrelated edit changed how many Chips the world carries");
  require(sortedPositions(first.getDetail()) ==
              sortedPositions(edited.getDetail()),
          "an unrelated edit elsewhere moved the Chips it did not touch");
}

// 7. The three existing outputs are untouched by the detail pass.
void theUnchippedOutputsAreIdenticalEitherWay() {
  auto primitives = slabAndPlatform(12.0f);
  auto arrangement = bw::core::arr::BuildArrangement(primitives);
  auto expectedTriangles = bw::core::arr::BuildArrangementTriangles(*arrangement);
  auto expectedWalls = bw::core::arr::BuildArrangementWalls(*arrangement);

  ArrangementWorldData snapshot(
      arrangement, wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}),
      64.0f, 8.0f);
  require(snapshot.getDetail().getChipCount() > 0,
          "the fixture produced no Chips, so this proves nothing");

  require(snapshot.getTriangles().size() == expectedTriangles.size(),
          "generating Chips changed the arrangement triangle count");
  for (size_t i = 0; i < expectedTriangles.size(); ++i) {
    auto const& a = snapshot.getTriangles()[i];
    auto const& b = expectedTriangles[i];
    require(a.face == b.face && a.v[0] == b.v[0] && a.v[1] == b.v[1] &&
                a.v[2] == b.v[2],
            "generating Chips changed an arrangement triangle");
  }
  require(snapshot.getWalls().size() == expectedWalls.size(),
          "generating Chips changed the wall count");
  for (size_t i = 0; i < expectedWalls.size(); ++i) {
    auto const& a = snapshot.getWalls()[i];
    auto const& b = expectedWalls[i];
    require(a.edge == b.edge && near(a.minZ, b.minZ) && near(a.maxZ, b.maxZ) &&
                a.kind == b.kind && a.paletteIndex == b.paletteIndex &&
                a.visible == b.visible,
            "generating Chips changed an arrangement wall");
  }

  // And the queries collision, floor height and picking all run through keep
  // reading that unchipped geometry: the platform's floor is flat at 12
  // right up to its own edge, where a Chip has visibly bitten into it.
  require(near(snapshot.getFloorHeight({0.0f, -19.0f}), 12.0f),
          "floor height read the chipped geometry instead of the unchipped world");
  require(snapshot.pointInTriangle({0.0f, -19.0f}) >= 0,
          "face containment lost ground a Chip only removed visually");
}
}  // namespace

int main() {
  try {
    everyFloorStepTopArrisCarriesOneChipAndNoBorderDoes();
    theChamferIsATaperedFortyFiveDegreeFacet();
    theFloorSideIsRebuiltWithTheFootprintSubtracted();
    replacementTrianglesCarryAnOutwardNormalTheyAreWoundAbout();
    aChipShrinksToFitItsWallsHeight();
    anInvisibleWallCarriesNoChip();
    chipsStayPutAcrossRegenerationAndUnrelatedEdits();
    theUnchippedOutputsAreIdenticalEitherWay();
    std::cout << "Chips are cut into FloorStep top Arrises and published in "
                 "the snapshot's detail channel\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
