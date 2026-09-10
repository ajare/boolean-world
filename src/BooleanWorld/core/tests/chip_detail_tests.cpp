#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
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
using bw::core::arr::DetailTriangleKind;

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

// Fixture Sub-material dimensions, resolved before arrangement construction.
float const chipDepth = 3.0f;
float const chipReach = 3.0f;

bw::core::ChipGenerationParameters fixedChip(float depth, float reach) {
  return {2.0f, depth, depth, reach, reach, 256.0f, 1.0f};
}

bw::core::ChipGenerationParameters fixedCornerChip(float distance) {
  bw::core::ChipGenerationParameters parameters;
  parameters.minimumCornerDistance = distance;
  parameters.maximumCornerDistance = distance;
  parameters.cornerProbability = 1.0f;
  return parameters;
}

Contour rectContour(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {{x0 * U, y0 * U}, {x1 * U, y0 * U}, {x1 * U, y1 * U}, {x0 * U, y1 * U}};
}

// One re-entrant boundary vertex at (0, 0), whose navigable angle is 270°.
Contour concaveContour() {
  return {
      {-20 * U, -20 * U}, {0, -20 * U}, {0, 0}, {20 * U, 0}, {20 * U, 20 * U}, {-20 * U, 20 * U}};
}

Contour reentrant225Contour() {
  return {
      {-20 * U, -20 * U}, {0, 0}, {20 * U, 0}, {20 * U, 20 * U}, {-20 * U, 20 * U}};
}

Contour reentrant315Contour() {
  return {
      {-20 * U, -20 * U}, {0, -20 * U}, {0, 0}, {20 * U, -20 * U}, {20 * U, 20 * U}, {-20 * U, 20 * U}};
}

PrimitivePropertySet propertiesWithHeights(float floorZ, float ceilingZ) {
  PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  return properties;
}

std::vector<std::vector<std::optional<bool>>> hiddenContour() {
  return {{false, false, false, false}};
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
         propertiesWithHeights(0.0f, 48.0f),
         {},
         hiddenContour()});
  }
  primitives.push_back(
      {{rectContour(-50, -50, 50, 50)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       1,
       uint32_t(primitives.size()),
       propertiesWithHeights(0.0f, 48.0f),
       {},
       hiddenContour()});
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

// The mirror image of slabAndPlatform: a 100x100 ground slab at floorZ 0,
// ceilingZ 48, with a 40x40 bulkhead in the middle whose own ceiling drops to
// bulkheadCeilingZ. The bulkhead's four sides are the only CeilingStep walls
// in the fixture; the slab's four sides are Borders. Reuses the platform's
// footprint so every numeric expectation below the Arris (which sits at
// bulkheadCeilingZ, exactly where the platform's Arris sat at
// platformFloorZ) carries over unchanged.
//
// `bulkheadVisibleOverrides` is indexed the same way as
// `platformVisibleOverrides` above.
std::vector<ArrangementPrimitive> slabAndBulkhead(
    float bulkheadCeilingZ,
    std::vector<std::optional<bool>> const& bulkheadVisibleOverrides = {},
    bool withDistantSlab = false) {
  std::vector<ArrangementPrimitive> primitives;
  if (withDistantSlab) {
    primitives.push_back(
        {{rectContour(200, 200, 240, 240)},
         Primitive::Operation::Union,
         Primitive::FillRule::EvenOdd,
         0,
         uint32_t(primitives.size()),
         propertiesWithHeights(0.0f, 48.0f),
         {},
         hiddenContour()});
  }
  primitives.push_back(
      {{rectContour(-50, -50, 50, 50)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       1,
       uint32_t(primitives.size()),
       propertiesWithHeights(0.0f, 48.0f),
       {},
       hiddenContour()});
  primitives.push_back(
      {{rectContour(-20, -20, 20, 20)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       2,
       uint32_t(primitives.size()),
       propertiesWithHeights(0.0f, bulkheadCeilingZ),
       {},
       bulkheadVisibleOverrides.empty()
           ? std::vector<std::vector<std::optional<bool>>>{}
           : std::vector<std::vector<std::optional<bool>>>{
                 bulkheadVisibleOverrides}});
  return primitives;
}

ArrangementWorldData snapshotOf(
    std::vector<ArrangementPrimitive> const& primitives,
    bw::core::ChipGenerationParameters const& chip =
        fixedChip(chipDepth, chipReach)) {
  auto resolved = primitives;
  for (auto& primitive : resolved) {
    primitive.chipParameters = chip;
  }
  return ArrangementWorldData(
      bw::core::arr::BuildArrangement(resolved),
      wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}),
      64.0f);
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
  require(detail.getChipCount() == floorSteps * 2,
          "the number of Chips did not match the Horizontal and Vertical Arrises");
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
    if (triangle.kind == DetailTriangleKind::HorizontalChipFacet) {
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
    if (triangle.kind == DetailTriangleKind::VerticalChipFacet) {
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
  auto expectedWallArea =
      40.0f * 12.0f - 3.0f * chipReach * chipDepth * 0.5f;
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

// 3b. Every replacement triangle carries one flat face normal calculated from
//     its geometry, shared by all three vertices, facing out of the solid and
//     with compatible anticlockwise winding.
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
    float geometric[3]{
        u[1] * v[2] - u[2] * v[1],
        u[2] * v[0] - u[0] * v[2],
        u[0] * v[1] - u[1] * v[0]};
    auto geometricLength = std::sqrt(
        geometric[0] * geometric[0] + geometric[1] * geometric[1] +
        geometric[2] * geometric[2]);
    auto facing = geometric[0] * n[0] + geometric[1] * n[1] +
                  geometric[2] * n[2];
    require(facing > 0.0f,
            "a replacement triangle was not wound anticlockwise about its normal");
    require(
        near(n[0], geometric[0] / geometricLength) &&
            near(n[1], geometric[1] / geometricLength) &&
            near(n[2], geometric[2] / geometricLength),
        "a replacement triangle did not carry its geometric face normal");
  }

  for (auto const& triangle :
       detail.replacementsFor(DetailSurfaceKind::FloorOfFace, platformFace)) {
    require(near(triangle.v[0].normal[2], 1.0f),
            "a rebuilt floor triangle's normal did not point straight up");
  }

  auto verifyWallAndFacetNormals = [](
                                       ArrangementWorldData const& worldData,
                                       ArrangementWallKind kind,
                                       wp::Vector2 const& midpoint,
                                       float expectedFacetZSign) {
    auto wallIndex = findWall(worldData, kind, midpoint);
    require(wallIndex != ~0u, "the wall used to verify normals was not found");
    auto const& wall = worldData.getWalls()[wallIndex];
    auto orientation = bw::core::arr::OrientArrangementWall(
        worldData.getArrangement(), wall);
    uint32_t facetCount = 0;
    for (auto const& triangle : worldData.getDetail().replacementsFor(
             DetailSurfaceKind::Wall, wallIndex)) {
      auto const& normal = triangle.v[0].normal;
      auto outward = normal[0] * orientation.normal.x +
                     normal[1] * orientation.normal.y;
      if (triangle.kind == DetailTriangleKind::SurfaceRemainder) {
        require(near(normal[0], orientation.normal.x) &&
                    near(normal[1], orientation.normal.y) &&
                    near(normal[2], 0.0f),
                "a wall-remainder normal did not face out of the solid");
        require(triangle.followsWallFacing,
                "a wall remainder was not marked to follow wall facing");
      } else if (triangle.kind ==
                 DetailTriangleKind::HorizontalChipFacet) {
        ++facetCount;
        require(outward > 0.0f,
                "a Chip facet normal pointed through the wall into the solid");
        require(normal[2] * expectedFacetZSign > 0.0f,
                "a Chip facet normal pointed into the horizontal solid");
        require(!triangle.followsWallFacing,
                "a Chip facet was marked for wall-normal mirroring");
      }
    }
    require(facetCount == 2,
            "the normal check did not inspect both facets of one Chip");
  };

  verifyWallAndFacetNormals(
      snapshot, ArrangementWallKind::FloorStep, {0.0f, -20.0f}, 1.0f);
  auto ceilingSnapshot = snapshotOf(slabAndBulkhead(12.0f));
  verifyWallAndFacetNormals(
      ceilingSnapshot, ArrangementWallKind::CeilingStep, {0.0f, -20.0f},
      -1.0f);
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
  require(detail.getChipCount() == 5,
          "hiding one wall did not remove its Horizontal and two Vertical Chips");

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
  bw::core::ChipGenerationParameters parameters{
      2.0f, 1.0f, 3.0f, 1.0f, 3.0f, 3.1f, 1.0f};
  parameters.types.assign(
      bw::core::AllChipTypes.begin(), bw::core::AllChipTypes.end());
  auto first = snapshotOf(slabAndPlatform(12.0f), parameters);
  auto again = snapshotOf(slabAndPlatform(12.0f), parameters);
  require(sortedPositions(first.getDetail()) ==
              sortedPositions(again.getDetail()),
          "regenerating an unchanged world moved its Chips");

  auto edited = snapshotOf(slabAndPlatform(12.0f, {}, true), parameters);
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

// 8. The mirror image of 1: exactly one Chip per CeilingStep's bottom Arris,
//    and none of a CeilingStep's own top - the wall's other end - or a
//    Border's.
void everyCeilingStepBottomArrisCarriesOneChipAndNoBorderOrTopDoes() {
  auto snapshot = snapshotOf(slabAndBulkhead(12.0f));
  auto const& detail = snapshot.getDetail();

  auto ceilingSteps = countWalls(snapshot, ArrangementWallKind::CeilingStep);
  require(
      ceilingSteps == 4,
      "the slab/bulkhead fixture did not produce four CeilingStep walls");
  require(detail.getChipCount() == ceilingSteps * 2,
          "the number of Chips did not match the Horizontal and Vertical Arrises");
  require(countWalls(snapshot, ArrangementWallKind::FloorStep) == 0,
          "the fixture produced a FloorStep it was not meant to");

  auto const& walls = snapshot.getWalls();
  for (uint32_t i = 0; i < uint32_t(walls.size()); ++i) {
    auto suppressed = detail.isSuppressed(DetailSurfaceKind::Wall, i);
    if (walls[i].kind == ArrangementWallKind::CeilingStep) {
      require(suppressed, "a CeilingStep wall's bottom Arris carried no Chip");
    } else {
      require(!suppressed,
              "a wall with no convex Arris - a Border - carried a Chip");
    }
  }
}

// 9. The mirror image of 2: the chamfer bites equally into the bulkhead's
//    ceiling and up the wall, at 45 degrees, tapering to nothing at both
//    ends.
void theCeilingChamferIsATaperedFortyFiveDegreeFacet() {
  auto snapshot = snapshotOf(slabAndBulkhead(12.0f));
  auto const& detail = snapshot.getDetail();

  // The bulkhead's south side: the Arris runs from (-20, -20) to (20, -20)
  // at z = 12, and the bulkhead - the lower-ceiling face - lies to the
  // north.
  auto wallIndex =
      findWall(snapshot, ArrangementWallKind::CeilingStep, {0.0f, -20.0f});
  require(wallIndex != ~0u, "the bulkhead's south CeilingStep was not found");
  auto const& wall = snapshot.getWalls()[wallIndex];
  require(near(wall.minZ, 12.0f) && near(wall.maxZ, 48.0f),
          "the bulkhead's south CeilingStep did not span the expected "
          "heights");

  auto replacements =
      detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex);
  require(!replacements.empty(),
          "a chipped wall published no replacement geometry");

  // Deepest at the Arris's centre: one point `chipDepth` into the bulkhead
  // ceiling, and one the same distance up the wall - the 45 degree bevel.
  require(hasVertexAt(replacements, 0.0f, -20.0f + chipDepth, 12.0f),
          "the Chip did not bite chipDepth into the horizontal face at the "
          "Arris's centre");
  require(hasVertexAt(replacements, 0.0f, -20.0f, 12.0f + chipDepth),
          "the Chip did not bite chipDepth up the wall at the Arris's "
          "centre");

  // Tapering to nothing at both ends: the facet meets the Arris again a half
  // reach either side of centre, at the Arris's own height and with no bite
  // at all.
  require(hasVertexAt(replacements, -chipReach * 0.5f, -20.0f, 12.0f) &&
              hasVertexAt(replacements, chipReach * 0.5f, -20.0f, 12.0f),
          "the Chip did not taper back to the Arris at both ends");

  uint32_t facetTriangles = 0;
  float wallPlaneArea = 0.0f;
  for (auto const& triangle : replacements) {
    if (triangle.kind == DetailTriangleKind::HorizontalChipFacet) {
      ++facetTriangles;
      require(
          std::any_of(
              triangle.v.begin(), triangle.v.end(),
              [](auto const& vertex) {
                return near(vertex.position[0], 0.0f) &&
                       near(vertex.position[1], -20.0f) &&
                       near(vertex.position[2], 12.0f + chipDepth);
              }),
          "a chamfer triangle did not meet the Chip's deepest cross-section");
      continue;
    }
    if (triangle.kind == DetailTriangleKind::VerticalChipFacet) {
      continue;
    }
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

  auto expectedWallArea =
      40.0f * (wall.maxZ - wall.minZ) -
      3.0f * chipReach * chipDepth * 0.5f;
  require(near(wallPlaneArea, expectedWallArea, 0.05f),
          "the chipped wall's remaining area was not its quad less the "
          "Chip's notch");
}

// 10. The mirror image of 3: the ceiling side of the bulkhead face is
//     rebuilt with the footprint subtracted, leaving its floor side (and the
//     surrounding slab) untouched, and every rebuilt triangle faces down.
void theCeilingSideIsRebuiltWithTheFootprintSubtracted() {
  auto snapshot = snapshotOf(slabAndBulkhead(12.0f));
  auto const& arrangement = snapshot.getArrangement();
  auto const& detail = snapshot.getDetail();

  auto bulkheadFace = snapshot.getContainingFaceIndex({0.0f, 0.0f});
  require(bulkheadFace != ~0u, "the bulkhead face was not found");
  require(
      near(arrangement.palette[arrangement.faces[bulkheadFace].paletteIndex]
               .ceilingZ,
           12.0f),
      "the located face was not the lowered bulkhead");

  require(
      detail.isSuppressed(DetailSurfaceKind::CeilingOfFace, bulkheadFace),
      "the bitten face's ceiling side was not suppressed");
  require(
      !detail.isSuppressed(DetailSurfaceKind::FloorOfFace, bulkheadFace),
      "suppressing the ceiling side also suppressed the floor underfoot");

  auto slabFace = snapshot.getContainingFaceIndex({0.0f, 35.0f});
  require(slabFace != ~0u && slabFace != bulkheadFace,
          "the surrounding slab face was not found");
  require(!detail.isSuppressed(DetailSurfaceKind::CeilingOfFace, slabFace),
          "a face no Chip bit into had its ceiling suppressed");

  float rebuiltArea = 0.0f;
  for (auto const& triangle : detail.replacementsFor(
           DetailSurfaceKind::CeilingOfFace, bulkheadFace)) {
    for (auto const& vertex : triangle.v) {
      require(near(vertex.position[2], 12.0f),
              "a rebuilt ceiling triangle left the face's ceiling plane");
      require(near(vertex.normal[2], -1.0f),
              "a rebuilt ceiling triangle did not face down");
    }
    rebuiltArea +=
        triangleArea2d(triangle.v[0].position, triangle.v[1].position,
                       triangle.v[2].position);
  }
  auto expectedArea = 40.0f * 40.0f - 4.0f * chipReach * chipDepth * 0.5f;
  require(near(rebuiltArea, expectedArea, 0.1f),
          "the rebuilt face's area was not its boundary less the four Chip "
          "footprints");
}

// 11. The mirror image of 4: the wall-height clamp shrinks a Chip rather
//     than letting it eat through the top of its own bulkhead.
void aCeilingChipShrinksToFitItsWallsHeight() {
  auto stepHeight = chipDepth * 0.5f;
  auto snapshot = snapshotOf(slabAndBulkhead(48.0f - stepHeight));
  auto const& detail = snapshot.getDetail();

  auto wallIndex =
      findWall(snapshot, ArrangementWallKind::CeilingStep, {0.0f, -20.0f});
  require(wallIndex != ~0u,
          "the shallow bulkhead's south CeilingStep was not found");
  auto replacements =
      detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex);
  require(!replacements.empty(), "the shallow bulkhead carried no Chip at all");

  for (auto const& triangle : replacements) {
    for (auto const& vertex : triangle.v) {
      require(vertex.position[2] <= 48.01f,
              "a Chip ate through the top of its own bulkhead");
    }
  }
  require(hasVertexAt(replacements, 0.0f, -20.0f, 48.0f),
          "the clamped Chip did not reach exactly the top of its bulkhead");
  require(hasVertexAt(
              replacements, 0.0f, -20.0f + stepHeight, 48.0f - stepHeight),
          "the clamped Chip's bite into the ceiling did not shrink to "
          "match");
}

// 12. The mirror image of 5: a wall turned off carries no Chip, and its
//     ceiling is not bitten either.
void anInvisibleCeilingStepWallCarriesNoChip() {
  std::vector<std::optional<bool>> allHidden(4, false);
  auto hidden = snapshotOf(slabAndBulkhead(12.0f, allHidden));
  require(hidden.getDetail().getChipCount() == 0,
          "walls whose visibility override is off still carried Chips");
  require(hidden.getDetail().getSuppressed().empty() &&
              hidden.getDetail().getTriangles().empty(),
          "an all-hidden bulkhead still published detail geometry");

  std::vector<std::optional<bool>> southHidden(4, std::nullopt);
  southHidden[0] = false;
  auto snapshot = snapshotOf(slabAndBulkhead(12.0f, southHidden));
  auto const& detail = snapshot.getDetail();
  require(detail.getChipCount() == 5,
          "hiding one wall did not remove its Horizontal and two Vertical Chips");

  auto bulkheadFace = snapshot.getContainingFaceIndex({0.0f, 0.0f});
  float rebuiltArea = 0.0f;
  for (auto const& triangle : detail.replacementsFor(
           DetailSurfaceKind::CeilingOfFace, bulkheadFace)) {
    rebuiltArea +=
        triangleArea2d(triangle.v[0].position, triangle.v[1].position,
                       triangle.v[2].position);
  }
  auto expectedArea = 40.0f * 40.0f - 3.0f * chipReach * chipDepth * 0.5f;
  require(near(rebuiltArea, expectedArea, 0.1f),
          "the hidden wall's Arris still bit into the horizontal face");
}

// 13. The Arris-length clamp: a Chip on a short Arris shrinks its reach to
//     the Arris's own length rather than running past either end.
void aChipShrinksItsReachToFitAShortArris() {
  std::vector<ArrangementPrimitive> primitives{
      {{rectContour(-50, -50, 50, 50)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesWithHeights(0.0f, 48.0f)},
      {{rectContour(-1, -20, 1, 20)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       1,
       1,
       propertiesWithHeights(12.0f, 48.0f)}};
  auto snapshot = snapshotOf(primitives);
  auto const& detail = snapshot.getDetail();

  // The platform's 2-unit Arris is shorter than the nominal reach (3), so it
  // must clamp its reach to its own length.
  auto wallIndex =
      findWall(snapshot, ArrangementWallKind::FloorStep, {0.0f, -20.0f});
  require(wallIndex != ~0u, "the short platform's south FloorStep was not found");
  auto replacements =
      detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex);
  require(!replacements.empty(), "the short Arris carried no Chip at all");

  // Tapering all the way to the Arris's own corners rather than chipReach's
  // nominal half-length.
  require(hasVertexAt(replacements, -1.0f, -20.0f, 12.0f) &&
              hasVertexAt(replacements, 1.0f, -20.0f, 12.0f),
          "the clamped Chip's reach did not shrink to exactly the Arris's own length");
  require(!hasVertexAt(replacements, -chipReach * 0.5f, -20.0f, 12.0f),
          "the clamped Chip still tapered at its nominal, unclamped reach");

  // Depth is untouched: the platform is wide enough, and tall enough, that
  // only the reach clamp applies.
  require(hasVertexAt(replacements, 0.0f, -20.0f + chipDepth, 12.0f),
          "the Arris-length clamp perturbed the Chip's depth");
}

// 14. The face-boundary clamp: a Chip on a narrow ledge shrinks its depth so
//     it cannot break through to the far side.
void aChipShrinksItsDepthToFitANarrowLedge() {
  std::vector<ArrangementPrimitive> primitives{
      {{rectContour(-50, -50, 50, 50)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesWithHeights(0.0f, 48.0f)},
      {{rectContour(-20, -1, 20, 1)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       1,
       1,
       propertiesWithHeights(12.0f, 48.0f)}};
  auto snapshot = snapshotOf(primitives);
  auto const& detail = snapshot.getDetail();

  // The ledge is only 2 world units deep (north-south), less than chipDepth
  // (3), so the south Arris's Chip must clamp its depth to the distance to
  // the ledge's north side rather than breaking through it.
  auto wallIndex =
      findWall(snapshot, ArrangementWallKind::FloorStep, {0.0f, -1.0f});
  require(wallIndex != ~0u, "the narrow ledge's south FloorStep was not found");
  auto replacements =
      detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex);
  require(!replacements.empty(), "the narrow ledge carried no Chip at all");

  for (auto const& triangle : replacements) {
    for (auto const& vertex : triangle.v) {
      require(vertex.position[1] <= 1.0f + 0.01f,
              "a Chip on a narrow ledge broke through to its far side");
    }
  }
  require(hasVertexAt(replacements, 0.0f, 1.0f, 12.0f),
          "the clamped Chip did not reach exactly the ledge's far boundary");

  // Reach is untouched: the ledge is long enough (40 units) that only the
  // face-boundary clamp applies.
  require(hasVertexAt(replacements, -chipReach * 0.5f, -1.0f, 12.0f) &&
              hasVertexAt(replacements, chipReach * 0.5f, -1.0f, 12.0f),
          "the face-boundary clamp perturbed the Chip's reach");
}

void cornerChipsTruncateFloorStepTrihedralVertices() {
  auto snapshot = snapshotOf(slabAndPlatform(12.0f), fixedCornerChip(2.0f));
  auto const& detail = snapshot.getDetail();
  require(detail.getChipCount() == 4,
          "the four FloorStep Corners did not each carry one Corner Chip");

  DetailTriangle const* southWest = nullptr;
  for (auto const& triangle : detail.getTriangles()) {
    if (triangle.kind == DetailTriangleKind::CornerChipFacet &&
        hasVertexAt(
            std::span<DetailTriangle const>{&triangle, 1}, -18.0f, -20.0f,
            12.0f) &&
        hasVertexAt(
            std::span<DetailTriangle const>{&triangle, 1}, -20.0f, -18.0f,
            12.0f) &&
        hasVertexAt(
            std::span<DetailTriangle const>{&triangle, 1}, -20.0f, -20.0f,
            10.0f)) {
      southWest = &triangle;
      break;
    }
  }
  require(southWest != nullptr,
          "a Corner Chip did not connect one point on each incident edge");
  auto const& normal = southWest->v[0].normal;
  require(
      normal == southWest->v[1].normal &&
          normal == southWest->v[2].normal,
      "a Corner Chip facet did not carry one flat geometric face normal");
  require(normal[0] < 0.0f && normal[1] < 0.0f && normal[2] > 0.0f,
          "a FloorStep Corner Chip facet did not face out of the cut");
  for (auto const& triangle : detail.getTriangles()) {
    if (triangle.kind == DetailTriangleKind::SurfaceRemainder) {
      require(
          !hasVertexAt(
              std::span<DetailTriangle const>{&triangle, 1}, -20.0f, -20.0f,
              12.0f),
          "a Corner Chip left the central vertex in an incident face");
    }
  }
}

void cornerChipsTruncateCeilingStepTrihedralVertices() {
  auto snapshot = snapshotOf(slabAndBulkhead(36.0f), fixedCornerChip(2.0f));
  require(snapshot.getDetail().getChipCount() == 4,
          "the four CeilingStep Corners did not each carry one Corner Chip");
  require(
      hasVertexAt(
          snapshot.getDetail().getTriangles(), -18.0f, -20.0f, 36.0f) &&
          hasVertexAt(
              snapshot.getDetail().getTriangles(), -20.0f, -18.0f, 36.0f) &&
          hasVertexAt(
              snapshot.getDetail().getTriangles(), -20.0f, -20.0f, 38.0f),
      "a CeilingStep Corner Chip did not cut along all three incident edges");
}

void cornerChipMinimumDistanceMustFitEveryIncidentEdge() {
  auto tooTall = snapshotOf(slabAndPlatform(1.5f), fixedCornerChip(2.0f));
  require(tooTall.getDetail().getChipCount() == 0,
          "a Corner Chip was placed when its minimum distance exceeded the vertical edge");

  auto exactFit = snapshotOf(slabAndPlatform(2.0f), fixedCornerChip(2.0f));
  require(exactFit.getDetail().getChipCount() == 4,
          "a Corner Chip whose minimum distance exactly fit was rejected");
}

void cornerChipDistancesAreRandomDeterministicAndGeometrySeeded() {
  auto parameters = fixedCornerChip(1.0f);
  parameters.maximumCornerDistance = 3.0f;
  auto first = snapshotOf(slabAndPlatform(12.0f), parameters);
  auto repeated = snapshotOf(slabAndPlatform(12.0f), parameters);
  auto renumbered = snapshotOf(slabAndPlatform(12.0f, {}, true), parameters);
  require(sortedPositions(first.getDetail()) ==
              sortedPositions(repeated.getDetail()),
          "Corner Chip distances changed across identical generations");
  require(sortedPositions(first.getDetail()) ==
              sortedPositions(renumbered.getDetail()),
          "Corner Chip distances depended on arrangement indices");

  std::vector<float> distances;
  for (auto const& triangle : first.getDetail().getTriangles()) {
    if (triangle.kind != DetailTriangleKind::CornerChipFacet) {
      continue;
    }
    for (auto const& vertex : triangle.v) {
      if (near(vertex.position[1], -20.0f) && vertex.position[0] > -19.99f) {
        distances.push_back(vertex.position[0] + 20.0f);
      } else if (
          near(vertex.position[0], -20.0f) &&
          vertex.position[1] > -19.99f) {
        distances.push_back(vertex.position[1] + 20.0f);
      } else if (
          near(vertex.position[0], -20.0f) &&
          near(vertex.position[1], -20.0f)) {
        distances.push_back(12.0f - vertex.position[2]);
      }
    }
    if (distances.size() == 3) {
      break;
    }
  }
  require(distances.size() == 3,
          "the three randomized Corner Chip distances were not emitted");
  for (auto distance : distances) {
    require(distance >= 1.0f - 0.01f && distance <= 3.0f + 0.01f,
            "a Corner Chip distance fell outside its Sub-material range");
  }
  require(
      !near(distances[0], distances[1], 0.001f) ||
          !near(distances[1], distances[2], 0.001f),
      "the three Corner Chip edge distances were not chosen independently");
}

void floorStepSubMaterialControlsCornerChips() {
  auto enabled = fixedCornerChip(2.0f);
  auto primitives = slabAndPlatform(12.0f);
  primitives[0].chipParameters = {};
  primitives[1].chipParameters = enabled;
  ArrangementWorldData higherFloorEnabled(
      bw::core::arr::BuildArrangement(primitives),
      wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}), 64.0f);
  require(higherFloorEnabled.getDetail().getChipCount() == 4,
          "the higher-floor Sub-material did not enable its FloorStep Corner Chips");

  primitives[0].chipParameters = enabled;
  primitives[1].chipParameters = {};
  ArrangementWorldData lowerFloorEnabled(
      bw::core::arr::BuildArrangement(primitives),
      wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}), 64.0f);
  require(lowerFloorEnabled.getDetail().getChipCount() == 0,
          "the lower-floor Sub-material overrode the FloorStep Sub-material");
}

void cornerAndArrisChipsDoNotOverlap() {
  auto parameters = fixedChip(2.0f, 2.0f);
  parameters.minimumCornerDistance = 2.0f;
  parameters.maximumCornerDistance = 2.0f;
  parameters.cornerProbability = 1.0f;
  auto snapshot = snapshotOf(slabAndPlatform(12.0f), parameters);
  require(snapshot.getDetail().getChipCount() == 12,
          "Corner endpoint reservations displaced an unexpected number of Arris Chips");
  for (auto const& triangle : snapshot.getDetail().getTriangles()) {
    auto const& a = triangle.v[0].position;
    auto const& b = triangle.v[1].position;
    auto const& c = triangle.v[2].position;
    float u[3]{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float v[3]{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    auto areaSquared =
        std::pow(u[1] * v[2] - u[2] * v[1], 2.0f) +
        std::pow(u[2] * v[0] - u[0] * v[2], 2.0f) +
        std::pow(u[0] * v[1] - u[1] * v[0], 2.0f);
    require(areaSquared > 0.0001f,
            "combined Corner and Arris cuts emitted degenerate geometry");
  }
}

void frontSideAnglesInRangeChipAlongTheSharedEdge() {
  std::vector<ArrangementPrimitive> primitives{
      {{concaveContour()},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesWithHeights(0.0f, 48.0f)}};
  auto snapshot = snapshotOf(primitives);
  auto const& detail = snapshot.getDetail();
  require(detail.getChipCount() == 1,
          "the 270-degree front-side wall angle did not carry one Vertical Chip");

  auto const& triangles = detail.getTriangles();
  require(
      hasVertexAt(triangles, 0.0f, 0.0f, 22.5f) &&
          hasVertexAt(triangles, 0.0f, 0.0f, 25.5f),
      "a Vertical Chip was not centred on the edge shared by its walls");
  require(
      hasVertexAt(triangles, 3.0f, 0.0f, 24.0f) &&
          hasVertexAt(triangles, 0.0f, -3.0f, 24.0f),
      "a Vertical Chip did not remove its footprint from both walls");
  constexpr float DiagonalDepth = 2.12132034f;  // 3 / sqrt(2)
  require(
      hasVertexAt(triangles, DiagonalDepth, -DiagonalDepth, 24.0f),
      "a Vertical Chip's deepest point did not go into the edge material");

  uint32_t edgeFacets = 0;
  bool facesUp = false;
  bool facesDown = false;
  for (auto const& triangle : triangles) {
    auto oneTriangle = std::span<DetailTriangle const>{&triangle, 1};
    if (triangle.kind != DetailTriangleKind::VerticalChipFacet ||
        (!hasVertexAt(oneTriangle, 0.0f, 0.0f, 22.5f) &&
         !hasVertexAt(oneTriangle, 0.0f, 0.0f, 25.5f))) {
      continue;
    }
    ++edgeFacets;
    auto const& normal = triangle.v[0].normal;
    require(-normal[0] + normal[1] > 0.0f,
            "a Vertical Chip face normal did not point out of its cavity");
    facesUp |= normal[2] > 0.0f;
    facesDown |= normal[2] < 0.0f;
  }
  require(edgeFacets == 4 && facesUp && facesDown,
          "the shared wall edge did not emit four inward-gouge facets");
}

void ninetyDegreeFrontSideAnglesDoNotChip() {
  std::vector<ArrangementPrimitive> primitives{
      {{rectContour(-20, -20, 20, 20)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesWithHeights(0.0f, 48.0f)}};
  auto snapshot = snapshotOf(primitives);
  require(snapshot.getDetail().getChipCount() == 0,
          "a 90-degree front-side wall angle carried a Vertical Chip");
}

void acuteFrontSideAnglesDoNotChip() {
  Contour acuteTriangle{
      {-20 * U, -10 * U}, {20 * U, -10 * U}, {0, 25 * U}};
  std::vector<ArrangementPrimitive> primitives{
      {{acuteTriangle},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesWithHeights(0.0f, 48.0f)}};
  auto snapshot = snapshotOf(primitives);
  require(snapshot.getDetail().getChipCount() == 0,
          "an acute front-side wall angle carried a Vertical Chip");
}

void verticalArrisAngleRangeIncludesBothEndpoints() {
  auto chipCountFor = [](Contour contour) {
    std::vector<ArrangementPrimitive> primitives{
        {{std::move(contour)},
         Primitive::Operation::Union,
         Primitive::FillRule::EvenOdd,
         0,
         0,
         propertiesWithHeights(0.0f, 48.0f)}};
    return snapshotOf(primitives).getDetail().getChipCount();
  };
  require(chipCountFor(reentrant225Contour()) == 1,
          "the inclusive 225-degree Vertical Arris did not chip");
  require(chipCountFor(reentrant315Contour()) == 1,
          "the inclusive 315-degree Vertical Arris did not chip");
}

void verticalArrisesUseTheSameRandomCountAndSpacingConfiguration() {
  std::vector<ArrangementPrimitive> primitives{
      {{concaveContour()},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesWithHeights(0.0f, 48.0f)}};
  bw::core::ChipGenerationParameters parameters{
      2.0f, 1.0f, 3.0f, 1.0f, 3.0f, 3.1f, 1.0f};
  auto snapshot = snapshotOf(primitives, parameters);
  // floor((48 - 3) / 3.1) + 1 = 15 on the one eligible Vertical Arris.
  require(snapshot.getDetail().getChipCount() == 15,
          "the Vertical Arris did not use the configured Chip count rule");

  std::vector<float> arrisPoints;
  for (auto const& triangle : snapshot.getDetail().getTriangles()) {
    if (triangle.followsWallFacing) {
      continue;
    }
    for (auto const& vertex : triangle.v) {
      if (near(vertex.position[0], 0.0f) &&
          near(vertex.position[1], 0.0f)) {
        arrisPoints.push_back(vertex.position[2]);
      }
    }
  }
  std::sort(arrisPoints.begin(), arrisPoints.end());
  arrisPoints.erase(
      std::unique(
          arrisPoints.begin(), arrisPoints.end(),
          [](float a, float b) { return near(a, b); }),
      arrisPoints.end());
  require(arrisPoints.size() == 30,
          "the expected Vertical Chip endpoints were not emitted");
  std::vector<float> centres;
  for (size_t i = 0; i < arrisPoints.size(); i += 2) {
    auto reach = arrisPoints[i + 1] - arrisPoints[i];
    require(reach >= 1.0f - 0.01f && reach <= 3.0f + 0.01f,
            "a Vertical Chip reach fell outside its configured range");
    centres.push_back((arrisPoints[i] + arrisPoints[i + 1]) * 0.5f);
  }
  for (size_t i = 1; i < centres.size(); ++i) {
    require(centres[i] - centres[i - 1] >= 3.1f - 0.01f,
            "Vertical Chips violated configured minimum spacing");
  }
}

void wallsCanCarryHorizontalAndVerticalChipsTogether() {
  std::vector<ArrangementPrimitive> primitives{
      {{rectContour(-50, -50, 50, 50)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesWithHeights(0.0f, 48.0f),
       {},
       hiddenContour()},
      {{concaveContour()},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       1,
       1,
       propertiesWithHeights(12.0f, 48.0f)}};
  auto snapshot = snapshotOf(primitives);
  require(snapshot.getDetail().getChipCount() == 11,
          "the platform did not carry six Horizontal and five Vertical Chips");

  for (auto const& triangle : snapshot.getDetail().getTriangles()) {
    auto const& a = triangle.v[0].position;
    auto const& b = triangle.v[1].position;
    auto const& c = triangle.v[2].position;
    float u[3]{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float v[3]{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    auto areaSquared =
        std::pow(u[1] * v[2] - u[2] * v[1], 2.0f) +
        std::pow(u[2] * v[0] - u[0] * v[2], 2.0f) +
        std::pow(u[0] * v[1] - u[1] * v[0], 2.0f);
    require(areaSquared > 0.0001f,
            "combined Arris notches emitted degenerate wall geometry");
  }
}

uint32_t verticalChipCount(DetailGeometry const& detail) {
  return uint32_t(std::count_if(
             detail.getTriangles().begin(), detail.getTriangles().end(),
             [](DetailTriangle const& triangle) {
               return triangle.kind == DetailTriangleKind::VerticalChipFacet;
             })) /
         4;
}

void verticalArrisesBetweenDifferentSubMaterialsDoNotChip() {
  auto propertiesA = propertiesWithHeights(0.0f, 48.0f);
  propertiesA.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("stone_a");
  auto propertiesB = propertiesWithHeights(0.0f, 48.0f);
  propertiesB.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("stone_b");
  std::vector<ArrangementPrimitive> primitives{
      {{rectContour(-20, -20, 0, 20)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       0,
       0,
       propertiesA},
      {{rectContour(0, 0, 20, 20)},
       Primitive::Operation::Union,
       Primitive::FillRule::EvenOdd,
       1,
       1,
       propertiesB}};

  auto differentMaterials = snapshotOf(primitives);
  require(verticalChipCount(differentMaterials.getDetail()) == 0,
          "a Vertical Arris between different Sub-materials chipped");

  primitives[1].properties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("stone_a");
  auto sameMaterial = snapshotOf(primitives);
  require(verticalChipCount(sameMaterial.getDetail()) == 1,
          "the same-material control Vertical Arris did not chip");
}

void probabilityAndMinimumArrisLengthControlEligibility() {
  auto disabled = snapshotOf(
      slabAndPlatform(12.0f), bw::core::ChipGenerationParameters{});
  require(disabled.getDetail().getChipCount() == 0,
          "the default zero probability still produced Chips");

  bw::core::ChipGenerationParameters tooShort{
      41.0f, 1.0f, 3.0f, 1.0f, 3.0f, 3.1f, 1.0f};
  auto shortArrises = snapshotOf(slabAndPlatform(12.0f), tooShort);
  require(shortArrises.getDetail().getChipCount() == 0,
          "an Arris below the material's minimum length produced Chips");
}

void everyArrisChipTypeBuildsOnHorizontalAndVerticalEdges() {
  std::set<std::vector<std::array<float, 3>>> signatures;
  for (auto type : bw::core::AllChipTypes) {
    auto parameters = fixedChip(2.0f, 2.0f);
    parameters.types = {type};
    auto snapshot = snapshotOf(slabAndPlatform(12.0f), parameters);
    require(snapshot.getDetail().getChipCount() == 8,
            "an Arris Chip type changed the number of eligible Chips");
    bool horizontal = false;
    bool vertical = false;
    for (auto const& triangle : snapshot.getDetail().getTriangles()) {
      if (triangle.kind != DetailTriangleKind::HorizontalChipFacet &&
          triangle.kind != DetailTriangleKind::VerticalChipFacet) {
        continue;
      }
      require(triangle.chipType == type,
              "an Arris facet lost its randomly selected Chip type");
      horizontal |= triangle.kind == DetailTriangleKind::HorizontalChipFacet;
      vertical |= triangle.kind == DetailTriangleKind::VerticalChipFacet;
      auto const& a = triangle.v[0].position;
      auto const& b = triangle.v[1].position;
      auto const& c = triangle.v[2].position;
      float u[3]{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
      float v[3]{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
      auto areaSquared =
          std::pow(u[1] * v[2] - u[2] * v[1], 2.0f) +
          std::pow(u[2] * v[0] - u[0] * v[2], 2.0f) +
          std::pow(u[0] * v[1] - u[1] * v[0], 2.0f);
      require(areaSquared > 0.0001f,
              "an Arris Chip type emitted a degenerate facet");
    }
    require(horizontal && vertical,
            "an Arris Chip type was not built in both orientations");
    signatures.insert(sortedPositions(snapshot.getDetail()));
  }
  require(signatures.size() == bw::core::AllChipTypes.size(),
          "two named Arris Chip types produced identical geometry");
}

void eachArrisChipChoosesFromItsSubMaterialsTypeList() {
  bw::core::ChipGenerationParameters parameters{
      2.0f, 1.0f, 3.0f, 1.0f, 3.0f, 3.1f, 1.0f};
  parameters.types.assign(
      bw::core::AllChipTypes.begin(), bw::core::AllChipTypes.end());
  auto first = snapshotOf(slabAndPlatform(12.0f), parameters);
  auto repeated = snapshotOf(slabAndPlatform(12.0f), parameters);
  require(sortedPositions(first.getDetail()) ==
              sortedPositions(repeated.getDetail()),
          "random Arris Chip type selection was not deterministic");
  std::set<bw::core::ChipType> selected;
  for (auto const& triangle : first.getDetail().getTriangles()) {
    if (triangle.chipType) {
      selected.insert(*triangle.chipType);
    }
  }
  require(selected.size() == bw::core::AllChipTypes.size(),
          "the authored Arris Chip type list was not sampled");

  auto wallIndex =
      findWall(first, ArrangementWallKind::FloorStep, {0.0f, -20.0f});
  auto replacements = first.getDetail().replacementsFor(
      DetailSurfaceKind::Wall, wallIndex);
  std::vector<float> endpoints;
  for (auto const& triangle : replacements) {
    if (triangle.kind != DetailTriangleKind::HorizontalChipFacet) {
      continue;
    }
    for (auto const& vertex : triangle.v) {
      if (near(vertex.position[1], -20.0f) &&
          near(vertex.position[2], 12.0f)) {
        endpoints.push_back(vertex.position[0]);
      }
    }
  }
  std::sort(endpoints.begin(), endpoints.end());
  endpoints.erase(
      std::unique(
          endpoints.begin(), endpoints.end(),
          [](float a, float b) { return near(a, b); }),
      endpoints.end());
  require(endpoints.size() == 24,
          "typed Arris Chips did not preserve their authored widths");
  float previousEnd = -1.0e9f;
  float previousCentre = -1.0e9f;
  for (size_t i = 0; i < endpoints.size(); i += 2) {
    auto reach = endpoints[i + 1] - endpoints[i];
    auto centre = (endpoints[i + 1] + endpoints[i]) * 0.5f;
    require(reach >= 1.0f - 0.01f && reach <= 3.0f + 0.01f,
            "a typed Arris Chip did not respect its selected width");
    require(endpoints[i] >= previousEnd - 0.01f &&
                centre - previousCentre >= 3.1f - 0.01f,
            "differently typed Arris Chips overlapped");
    previousEnd = endpoints[i + 1];
    previousCentre = centre;
  }
}

void chipsUseRandomSizesAndNonOverlappingRandomPositions() {
  bw::core::ChipGenerationParameters parameters{
      2.0f, 1.0f, 3.0f, 1.0f, 3.0f, 3.1f, 1.0f};
  auto snapshot = snapshotOf(slabAndPlatform(12.0f), parameters);
  auto const& detail = snapshot.getDetail();
  // floor((40 - maxReach) / spacing) + 1 = 12 per Arris, four Arrises.
  require(detail.getChipCount() >= 48 && detail.getChipCount() <= 60,
          "probability one did not fill every non-overlapping Chip slot");

  auto wallIndex =
      findWall(snapshot, ArrangementWallKind::FloorStep, {0.0f, -20.0f});
  auto replacements =
      detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex);
  std::vector<float> arrisPoints;
  std::vector<float> depths;
  for (auto const& triangle : replacements) {
    if (triangle.kind != DetailTriangleKind::HorizontalChipFacet) {
      continue;
    }
    for (auto const& vertex : triangle.v) {
      if (near(vertex.position[1], -20.0f) &&
          near(vertex.position[2], 12.0f)) {
        arrisPoints.push_back(vertex.position[0]);
      }
      if (near(vertex.position[1], -20.0f) && vertex.position[2] < 11.99f) {
        depths.push_back(12.0f - vertex.position[2]);
      }
    }
  }
  auto uniqueSorted = [](std::vector<float> values) {
    std::sort(values.begin(), values.end());
    values.erase(
        std::unique(values.begin(), values.end(),
                    [](float a, float b) { return near(a, b); }),
        values.end());
    return values;
  };
  arrisPoints = uniqueSorted(std::move(arrisPoints));
  depths = uniqueSorted(std::move(depths));
  require(arrisPoints.size() == 24 && depths.size() == 12,
          "the expected randomized chamfer geometry was not emitted");

  std::vector<float> centres;
  for (size_t i = 0; i < arrisPoints.size(); i += 2) {
    auto reach = arrisPoints[i + 1] - arrisPoints[i];
    require(reach >= 1.0f - 0.01f && reach <= 3.0f + 0.01f,
            "a randomized Chip reach fell outside its authored range");
    centres.push_back((arrisPoints[i] + arrisPoints[i + 1]) * 0.5f);
  }
  bool irregular = false;
  for (size_t i = 1; i < centres.size(); ++i) {
    auto spacing = centres[i] - centres[i - 1];
    require(spacing >= 3.1f - 0.01f,
            "randomly placed Chips violated minimum spacing");
    if (i > 1 && !near(
                     spacing, centres[i - 1] - centres[i - 2], 0.05f)) {
      irregular = true;
    }
  }
  require(irregular, "multiple Chips were spaced evenly rather than randomly");
  for (auto depth : depths) {
    require(depth >= 1.0f - 0.01f && depth <= 3.0f + 0.01f,
            "a randomized Chip depth fell outside its authored range");
  }
}

void nonHorizontalArrisesDoNotProduceScalarHeightChips() {
  auto slopedFloor = slabAndPlatform(12.0f);
  slopedFloor.back().properties.floorZ.gradient = {0.05f, 0.0f};
  auto floorSnapshot = snapshotOf(slopedFloor, fixedCornerChip(2.0f));
  require(floorSnapshot.getDetail().getChipCount() == 0 &&
              floorSnapshot.getDetail().getTriangles().empty(),
          "a sloped floor Arris produced scalar-height Chip geometry");

  auto slopedCeiling = slabAndBulkhead(36.0f);
  slopedCeiling.back().properties.ceilingZ.gradient = {0.05f, 0.0f};
  auto ceilingSnapshot = snapshotOf(slopedCeiling);
  require(ceilingSnapshot.getDetail().getChipCount() == 0 &&
              ceilingSnapshot.getDetail().getTriangles().empty(),
          "a sloped ceiling Arris produced scalar-height Chip geometry");

  auto properties = propertiesWithHeights(0.0f, 48.0f);
  properties.floorZ.gradient = {0.05f, 0.0f};
  auto verticalSnapshot = snapshotOf({{{concaveContour()}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, 1, 0, properties}});
  require(verticalChipCount(verticalSnapshot.getDetail()) == 0,
          "walls bounded by a sloped surface produced a scalar-height Vertical Chip");
}

// Resolved dimensions belong to each wall's own Sub-material palette entry:
// a disabled material can coexist with one that chips.
void aDisabledMaterialDoesNotDisableOtherMaterialsChips() {
  std::vector<ArrangementPrimitive> primitives;
  auto add = [&](Contour contour, float floorZ,
                 bw::core::ChipGenerationParameters parameters) {
    auto properties = propertiesWithHeights(floorZ, 48.0f);
    primitives.push_back(
        {{std::move(contour)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, uint64_t(primitives.size() + 1), uint32_t(primitives.size()), properties, {}, {}, parameters});
  };
  add(rectContour(-50, -20, -10, 20), 0.0f, {});
  add(rectContour(-40, -10, -20, 10), 12.0f, {});
  add(rectContour(10, -20, 50, 20), 0.0f,
      fixedChip(chipDepth, chipReach));
  add(rectContour(20, -10, 40, 10), 12.0f,
      fixedChip(chipDepth, chipReach));

  ArrangementWorldData snapshot(
      bw::core::arr::BuildArrangement(primitives),
      wp::BoundingBox({-64.0f, -64.0f}, {128.0f, 128.0f}), 32.0f);
  require(snapshot.getDetail().getChipCount() == 8,
          "a disabled wall material chipped or disabled the other material");
}

// 15. The minimum-size drop: a Chip clamped smaller than the minimum is not
//     emitted at all, and the surface it would have bitten is left whole.
void aChipBelowTheMinimumSizeIsDroppedEntirely() {
  auto snapshot = snapshotOf(slabAndPlatform(0.001f));
  auto const& detail = snapshot.getDetail();
  require(detail.getChipCount() == 0,
          "a step shallower than the minimum Chip size still carried Chips");
  require(detail.getSuppressed().empty() && detail.getTriangles().empty(),
          "a fully clamped-away Chip still published detail geometry");
}

// 7. The three existing outputs are untouched by the detail pass.
void theUnchippedOutputsAreIdenticalEitherWay() {
  auto primitives = slabAndPlatform(12.0f);
  for (auto& primitive : primitives) {
    primitive.chipParameters = fixedChip(chipDepth, chipReach);
  }
  auto arrangement = bw::core::arr::BuildArrangement(primitives);
  auto expectedTriangles = bw::core::arr::BuildArrangementTriangles(*arrangement);
  auto expectedWalls = bw::core::arr::BuildArrangementWalls(*arrangement);

  ArrangementWorldData snapshot(
      arrangement, wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}),
      64.0f);
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
    everyCeilingStepBottomArrisCarriesOneChipAndNoBorderOrTopDoes();
    theCeilingChamferIsATaperedFortyFiveDegreeFacet();
    theCeilingSideIsRebuiltWithTheFootprintSubtracted();
    aCeilingChipShrinksToFitItsWallsHeight();
    anInvisibleCeilingStepWallCarriesNoChip();
    aChipShrinksItsReachToFitAShortArris();
    aChipShrinksItsDepthToFitANarrowLedge();
    cornerChipsTruncateFloorStepTrihedralVertices();
    cornerChipsTruncateCeilingStepTrihedralVertices();
    cornerChipMinimumDistanceMustFitEveryIncidentEdge();
    cornerChipDistancesAreRandomDeterministicAndGeometrySeeded();
    floorStepSubMaterialControlsCornerChips();
    cornerAndArrisChipsDoNotOverlap();
    frontSideAnglesInRangeChipAlongTheSharedEdge();
    ninetyDegreeFrontSideAnglesDoNotChip();
    acuteFrontSideAnglesDoNotChip();
    verticalArrisAngleRangeIncludesBothEndpoints();
    verticalArrisesUseTheSameRandomCountAndSpacingConfiguration();
    wallsCanCarryHorizontalAndVerticalChipsTogether();
    verticalArrisesBetweenDifferentSubMaterialsDoNotChip();
    probabilityAndMinimumArrisLengthControlEligibility();
    everyArrisChipTypeBuildsOnHorizontalAndVerticalEdges();
    eachArrisChipChoosesFromItsSubMaterialsTypeList();
    chipsUseRandomSizesAndNonOverlappingRandomPositions();
    nonHorizontalArrisesDoNotProduceScalarHeightChips();
    aDisabledMaterialDoesNotDisableOtherMaterialsChips();
    aChipBelowTheMinimumSizeIsDroppedEntirely();
    std::cout << "Chips are cut into eligible Arrises and trihedral Corners "
                 "and published in the snapshot's detail channel\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
