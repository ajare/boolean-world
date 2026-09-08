#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/PrimitivePropertySet.h>

namespace {
using bw::core::ArrangementWorldData;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;
using bw::core::arr::ToFixedPointCoordinate;

constexpr double Epsilon = 0.001;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(double actual, double expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon, message);
}

Contour rectangle(double minX, double minY, double maxX, double maxY) {
  auto fp = [](double v) { return ToFixedPointCoordinate(v); };
  return {{fp(minX), fp(minY)}, {fp(maxX), fp(minY)},
          {fp(maxX), fp(maxY)}, {fp(minX), fp(maxY)}};
}

PrimitivePropertySet properties(float floorZ, float ceilingZ, float liquidLevel) {
  PrimitivePropertySet result;
  result.floorZ = floorZ;
  result.ceilingZ = ceilingZ;
  result.liquidLevel = liquidLevel;
  return result;
}

// rawArea stands in for Primitive::getArea(), which these fixed-point-contour
// arrangement tests have no real Primitive to call - the caller supplies the
// same rectangle area it used to build the contour.
ArrangementPrimitive rectanglePrimitive(
    Contour contour, Primitive::Operation operation, uint8_t priority,
    uint32_t primitiveIndex, PrimitivePropertySet const& props, double rawArea) {
  ArrangementPrimitive result{
      {std::move(contour)}, operation, Primitive::FillRule::NonZero,
      priority, primitiveIndex, props};
  result.rawArea = rawArea;
  return result;
}

// A lone Union primitive is a solid room by itself - the same, ordinary way
// every room in an authored World is built (see world-test-1.world.yaml). Liquid
// physics operates over these solid faces: the ones BuildArrangementTriangles
// renders and the player walks on.
ArrangementPrimitive room(
    double minX, double minY, double maxX, double maxY,
    PrimitivePropertySet const& props) {
  return rectanglePrimitive(
      rectangle(minX, minY, maxX, maxY), Primitive::Operation::Union, 0, 1,
      props, (maxX - minX) * (maxY - minY));
}

// Locates the sole solid, non-exterior face, for tests whose arrangement has
// exactly one room of interest.
uint32_t soleSolidFace(bw::core::arr::ArrangementResult const& arrangement) {
  int found = -1;
  for (uint32_t i = 1; i < uint32_t(arrangement.faces.size()); ++i) {
    if (arrangement.faces[i].solid) {
      require(found < 0, "expected exactly one solid room face");
      found = int(i);
    }
  }
  require(found >= 0, "no solid room face was found");
  return uint32_t(found);
}

// Locates whichever face contains a given point, solid or not - for tests
// with more than one room to tell apart by position rather than by which
// primitive happens to own each one's properties.
uint32_t faceAt(
    bw::core::arr::ArrangementResult const& arrangement, double x, double y) {
  bw::core::arr::FixedPointVertex fixed{
      ToFixedPointCoordinate(x), ToFixedPointCoordinate(y)};
  for (uint32_t i = 1; i < uint32_t(arrangement.faces.size()); ++i) {
    if (bw::core::arr::PointInFace(fixed, arrangement.faces[i], arrangement)) {
      return i;
    }
  }
  require(false, "no face contains the given point");
  return ~0u;
}

void aUnionPrimitivesLiquidLevelFillsTheRoomItCovers() {
  // The room's rawArea equals its faceArea (nothing else carves it further),
  // so its authored liquid level passes straight through as depth.
  auto arrangement = bw::core::arr::BuildArrangement(
      {room(0, 0, 100, 100, properties(0.0f, 48.0f, 20.0f))});
  auto depths = bw::core::arr::ComputeUndistributedLiquidDepths(*arrangement);

  auto faceIndex = soleSolidFace(*arrangement);
  requireNear(depths[faceIndex], 20.0,
              "a Union primitive's liquid level should pass through to the room it alone covers");
}

void anUnsetLiquidLevelProducesZeroDepth() {
  auto arrangement = bw::core::arr::BuildArrangement(
      {room(0, 0, 100, 100, properties(0.0f, 48.0f, 0.0f))});
  auto depths = bw::core::arr::ComputeUndistributedLiquidDepths(*arrangement);

  auto faceIndex = soleSolidFace(*arrangement);
  requireNear(depths[faceIndex], 0.0,
              "a default, unset liquid level should produce zero depth");
}

// Total seeded volume: every solid face's depth times its own area.
double seededVolume(
    bw::core::arr::ArrangementResult const& arrangement,
    std::vector<float> const& depths) {
  double total = 0.0;
  for (uint32_t i = 1; i < uint32_t(arrangement.faces.size()); ++i) {
    if (!arrangement.faces[i].solid) continue;
    total += depths[i] * bw::core::arr::FaceArea(arrangement.faces[i], arrangement);
  }
  return total;
}

// Nothing is carved here: a second, higher-priority Union primitive covers
// part of the first, so the fold splits the first primitive's footprint into
// two solid faces of 6000 and 14000 without removing any of it. Subdividing a
// primitive must not change how much liquid it holds or how deep that liquid
// stands - and an author does not choose this split, any unrelated primitive
// whose edge crosses this one causes it. Seeding each face at
// liquidLevel * faceArea / rawArea (the original formula) made a face's
// volume scale with the square of its area, quietly destroying most of the
// liquid: here it would have yielded depths of 12 and 28 totalling 464000 of
// the authored 800000.
void subdividingAPrimitiveChangesNeitherItsDepthNorItsVolume() {
  auto base = rectanglePrimitive(
      rectangle(0, 0, 200, 100), Primitive::Operation::Union, 0, 1,
      properties(0.0f, 100.0f, 40.0f), 20000.0);
  auto overlay = rectanglePrimitive(
      rectangle(0, 0, 60, 100), Primitive::Operation::Union, 1, 2,
      properties(0.0f, 100.0f, 0.0f), 6000.0);

  auto arrangement = bw::core::arr::BuildArrangement({base, overlay});
  auto depths = bw::core::arr::ComputeUndistributedLiquidDepths(*arrangement);

  auto smallFace = faceAt(*arrangement, 30, 50);
  auto largeFace = faceAt(*arrangement, 140, 50);
  require(smallFace != largeFace,
          "the overlay should have split the base primitive into two faces");
  require(arrangement->faces[smallFace].solid && arrangement->faces[largeFace].solid,
          "both halves of the subdivided base primitive should remain solid");

  requireNear(depths[smallFace], 40.0,
              "a subdivided primitive's smaller face should still seed at the authored depth");
  requireNear(depths[largeFace], 40.0,
              "a subdivided primitive's larger face should still seed at the authored depth");
  requireNear(seededVolume(*arrangement, depths), 40.0 * 20000.0,
              "subdividing a primitive must not change its total liquid volume");
}

// Only half of the base primitive's footprint remains after a Difference
// carves the other half away. The authored volume is conserved rather than
// losing the carved share, so what is left stands correspondingly deeper -
// a pillar sunk into a flooded room displaces liquid rather than deleting it.
void aPartlyCarvedPrimitiveKeepsItsVolumeAndStandsDeeper() {
  auto base = rectanglePrimitive(
      rectangle(0, 0, 100, 100), Primitive::Operation::Union, 0, 1,
      properties(0.0f, 480.0f, 40.0f), 10000.0);
  auto cut = rectanglePrimitive(
      rectangle(0, 0, 50, 100), Primitive::Operation::Difference, 1, 2,
      properties(0.0f, 480.0f, 0.0f), 5000.0);

  auto arrangement = bw::core::arr::BuildArrangement({base, cut});
  auto depths = bw::core::arr::ComputeUndistributedLiquidDepths(*arrangement);

  auto roomFace = faceAt(*arrangement, 75, 50);
  require(arrangement->faces[roomFace].solid,
          "the uncarved half of the base slab should remain solid");

  requireNear(depths[roomFace], 80.0,
              "a half-carved Union primitive should stand twice as deep over the half that survives");
  requireNear(seededVolume(*arrangement, depths), 40.0 * 10000.0,
              "carving a primitive must not change its total liquid volume");
}

// A second, fully-overlapping primitive with a non-Union operation leaves the
// room solid (its membership still covers the same face) but must not
// contribute its own liquid level to that face's depth.
void aLiquidLevelOnANonUnionPrimitiveHasNoEffect() {
  auto base = rectanglePrimitive(
      rectangle(0, 0, 100, 100), Primitive::Operation::Union, 0, 1,
      properties(0.0f, 48.0f, 0.0f), 10000.0);
  auto overlay = rectanglePrimitive(
      rectangle(0, 0, 100, 100), Primitive::Operation::Intersection, 1, 2,
      properties(0.0f, 48.0f, 30.0f), 10000.0);

  auto arrangement = bw::core::arr::BuildArrangement({base, overlay});
  auto depths = bw::core::arr::ComputeUndistributedLiquidDepths(*arrangement);

  auto faceIndex = soleSolidFace(*arrangement);
  requireNear(depths[faceIndex], 0.0,
              "a liquid level authored on a non-Union primitive must have no effect on any face's depth");
}

// The undistributed depth is the seed volume the equilibrium pass spreads
// between faces, so it deliberately overshoots its own face's clearance
// rather than destroying the volume that has to flow onward.
void anUndistributedDepthIsNotCappedAtTheFacesClearance() {
  auto arrangement = bw::core::arr::BuildArrangement(
      {room(0, 0, 100, 100, properties(10.0f, 34.0f, 1000.0f))});
  auto depths = bw::core::arr::ComputeUndistributedLiquidDepths(*arrangement);

  auto faceIndex = soleSolidFace(*arrangement);
  requireNear(depths[faceIndex], 1000.0,
              "the undistributed depth should carry the whole authored volume, "
              "leaving the ceiling cap to the equilibrium pass");
}

void getLiquidDepthQueriesTheContainingFace() {
  // A thin solid rim around the room, its floor pinned exactly to the room's
  // ceiling so the shared clearance is zero - a real wall the room cannot
  // equilibrate across - so the room stays sealed off from the exterior
  // drain rather than settling at zero.
  auto rim = rectanglePrimitive(
      rectangle(-10, -10, 110, 110), Primitive::Operation::Union, 0, 1,
      properties(48.0f, 48.0f, 0.0f), 14400.0);
  auto sourceProperties = properties(0.0f, 48.0f, 18.0f);
  sourceProperties.floorZ.gradient = {0.1f, 0.0f};
  auto source = rectanglePrimitive(
      rectangle(0, 0, 100, 100), Primitive::Operation::Union, 1, 2,
      sourceProperties, 10000.0);

  ArrangementWorldData worldData(
      bw::core::arr::BuildArrangement({rim, source}),
      wp::BoundingBox({-256.0f, -256.0f}, {512.0f, 512.0f}), 64.0f);

  requireNear(worldData.getLiquidDepth({50.0f, 50.0f}), 18.0,
              "getLiquidDepth should return the containing face's computed depth");
  requireNear(worldData.getLiquidSurfaceHeight({20.0f, 50.0f}), 20.0,
              "the Liquid surface query did not sample its position");
  requireNear(worldData.getLiquidSurfaceHeight({80.0f, 50.0f}), 26.0,
              "the Liquid surface query used a face-wide floor elevation");
  requireNear(worldData.getLiquidDepth({-200.0f, -200.0f}), 0.0,
              "getLiquidDepth outside the arrangement should be zero");
}

}  // namespace

int main() {
  try {
    aUnionPrimitivesLiquidLevelFillsTheRoomItCovers();
    anUnsetLiquidLevelProducesZeroDepth();
    subdividingAPrimitiveChangesNeitherItsDepthNorItsVolume();
    aPartlyCarvedPrimitiveKeepsItsVolumeAndStandsDeeper();
    aLiquidLevelOnANonUnionPrimitiveHasNoEffect();
    anUndistributedDepthIsNotCappedAtTheFacesClearance();
    getLiquidDepthQueriesTheContainingFace();
    std::cout << "ComputeUndistributedLiquidDepths distributes authored liquid level across covered faces\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
