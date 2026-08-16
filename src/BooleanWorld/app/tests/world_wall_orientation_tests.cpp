#include <cmath>
#include <iostream>
#include <stdexcept>

#include <core/Arrangement.h>

#include "WorldWallOrientation.h"

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(float actual, float expected, char const* message) {
  require(std::abs(actual - expected) < Epsilon, message);
}

bw::core::arr::ArrangementResult arrangement(
    bool face0Solid, bool face1Solid, float face0Floor, float face1Floor,
    float face0Ceiling, float face1Ceiling) {
  bw::core::arr::ArrangementResult result;
  result.vertices = {{0, 0}, {1000, 0}};
  result.edges.push_back({{0, 1}, {0, 1}});
  result.faces.resize(2);
  result.faces[0].solid = face0Solid;
  result.faces[1].solid = face1Solid;

  bw::core::PrimitivePropertySet properties0{};
  properties0.floorZ = face0Floor;
  properties0.ceilingZ = face0Ceiling;
  bw::core::PrimitivePropertySet properties1{};
  properties1.floorZ = face1Floor;
  properties1.ceilingZ = face1Ceiling;
  result.palette = {properties0, properties1};
  result.faces[0].paletteIndex = 0;
  result.faces[1].paletteIndex = 1;
  return result;
}

void requireFacesFace0Side(
    bw::core::arr::ArrangementWallKind kind,
    bool face0Solid, float face0Floor, float face1Floor,
    float face0Ceiling, float face1Ceiling, bool face0IsFront,
    char const* message) {
  auto result = arrangement(
      face0Solid,
      kind == bw::core::arr::ArrangementWallKind::Border ? !face0Solid : true,
      face0Floor, face1Floor, face0Ceiling, face1Ceiling);
  auto orientation = bw::app::orientArrangementWall(
      result, {0, 0.0f, 1.0f, 0, kind});

  // The edge is sorted left-to-right, so face 0 is north (the left side).
  requireNear(orientation.normal.x, 0.0f, message);
  requireNear(orientation.normal.y, face0IsFront ? 1.0f : -1.0f, message);
  requireNear(orientation.v0.x, face0IsFront ? 0.0f : 1.0f, message);
  requireNear(orientation.v1.x, face0IsFront ? 1.0f : 0.0f, message);
}

void wallsFaceTheirIncidentFrontSide() {
  // Border walls face the empty side, rather than the arbitrary sorted edge
  // direction. The first case is the audited regression: the left face is
  // solid, so its wall must face right.
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::Border,
      true, 0.0f, 0.0f, 2.0f, 2.0f, false,
      "border wall with a solid left face did not face the empty right side");
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::Border,
      false, 0.0f, 0.0f, 2.0f, 2.0f, true,
      "border wall with an empty left face did not face left");

  // Floor steps face the lower floor side.
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::FloorStep,
      true, 0.0f, 1.0f, 3.0f, 3.0f, true,
      "floor step did not face its lower left side");
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::FloorStep,
      true, 1.0f, 0.0f, 3.0f, 3.0f, false,
      "floor step did not face its lower right side");

  // Ceiling steps face the higher ceiling side.
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::CeilingStep,
      true, 0.0f, 0.0f, 3.0f, 2.0f, true,
      "ceiling step did not face its higher left side");
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::CeilingStep,
      true, 0.0f, 0.0f, 2.0f, 3.0f, false,
      "ceiling step did not face its higher right side");
}

}  // namespace

int main() {
  try {
    wallsFaceTheirIncidentFrontSide();
    std::cout << "Arrangement walls face their incident front sides\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
