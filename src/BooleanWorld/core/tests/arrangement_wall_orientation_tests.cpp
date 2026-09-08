#include <cmath>
#include <iostream>
#include <stdexcept>

#include <core/Arrangement.h>

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
  auto orientation = bw::core::arr::OrientArrangementWall(
      result, {0, 0.0f, 1.0f, 0, kind});

  // The edge is sorted left-to-right, so face 0 is north (the left side).
  requireNear(orientation.normal.x, 0.0f, message);
  requireNear(orientation.normal.y, face0IsFront ? 1.0f : -1.0f, message);
  requireNear(orientation.v0.x, face0IsFront ? 0.0f : 1.0f, message);
  requireNear(orientation.v1.x, face0IsFront ? 1.0f : 0.0f, message);
}

void orientedElevationsFollowOrientedEndpoints() {
  auto result = arrangement(false, true, 1.0f, 0.0f, 3.0f, 3.0f);
  bw::core::arr::ArrangementWall wall{
      0, 1.0f, 7.0f, 1,
      bw::core::arr::ArrangementWallKind::FloorStep, 2.0f};
  wall.bottomZ = {1.0f, 2.0f};
  wall.topZ = {6.0f, 7.0f};

  auto orientation = bw::core::arr::OrientArrangementWall(result, wall);
  requireNear(orientation.v0.x, 1.0f,
              "a reversed wall did not reverse its first endpoint");
  requireNear(orientation.bottomZ[0], 2.0f,
              "a reversed wall detached its bottom from the first endpoint");
  requireNear(orientation.bottomZ[1], 1.0f,
              "a reversed wall detached its bottom from the second endpoint");
  requireNear(orientation.topZ[0], 7.0f,
              "a reversed wall detached its top from the first endpoint");
  requireNear(orientation.topZ[1], 6.0f,
              "a reversed wall detached its top from the second endpoint");
}

void wallsFaceTheirIncidentFrontSide() {
  // Border walls face the solid polygon rather than the empty exterior.
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::Border,
      true, 0.0f, 0.0f, 2.0f, 2.0f, true,
      "border wall with a solid left face did not face the polygon");
  requireFacesFace0Side(
      bw::core::arr::ArrangementWallKind::Border,
      false, 0.0f, 0.0f, 2.0f, 2.0f, false,
      "border wall with a solid right face did not face the polygon");

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
    orientedElevationsFollowOrientedEndpoints();
    std::cout << "Arrangement walls face their incident front sides\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
