#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::arr::ArrangementFace;
using bw::core::arr::ArrangementResult;
using bw::core::arr::FixedPointVertex;
using bw::core::arr::Membership;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int64_t triangleArea2(FixedPointVertex const& a,
                      FixedPointVertex const& b,
                      FixedPointVertex const& c) {
  return (b.x - a.x) * (c.y - a.y) -
         (b.y - a.y) * (c.x - a.x);
}

int64_t boundaryArea2(ArrangementResult const& arrangement,
                      std::vector<uint32_t> const& boundary) {
  int64_t area2 = 0;
  for (size_t i = 0; i < boundary.size(); ++i) {
    auto const& a = arrangement.vertices[boundary[i]];
    auto const& b = arrangement.vertices[boundary[(i + 1) % boundary.size()]];
    area2 += a.x * b.y - a.y * b.x;
  }
  return std::abs(area2);
}

std::vector<FixedPointVertex> narrowWorldEdgeSquare() {
  // Adjacent fixed-point vertices at the positive world edge must remain
  // distinct while earcut chooses the triangulation.
  return {{4'095'999, 4'095'999},
          {4'096'000, 4'095'999},
          {4'096'000, 4'096'000},
          {4'095'999, 4'096'000}};
}

int64_t triangulatedArea2(ArrangementResult const& arrangement) {
  int64_t area2 = 0;
  for (auto const& triangle :
       bw::core::arr::BuildArrangementTriangles(arrangement)) {
    area2 += std::abs(triangleArea2(arrangement.vertices[triangle.v[0]],
                                    arrangement.vertices[triangle.v[1]],
                                    arrangement.vertices[triangle.v[2]]));
  }
  return area2;
}

void triangulatesArrangementAtFixedPointPrecision() {
  ArrangementResult arrangement;
  arrangement.vertices = narrowWorldEdgeSquare();
  ArrangementFace face{{0, 1, 2, 3},
                       {0, 1, 2, 3},
                       {},
                       {},
                       Membership(0),
                       true};
  arrangement.faces.emplace_back(
      ArrangementFace{{}, {}, {}, {}, Membership(0)});
  arrangement.faces.emplace_back(std::move(face));
  arrangement.palette.emplace_back();

  auto triangles = bw::core::arr::BuildArrangementTriangles(arrangement);
  require(triangles.size() == 2,
          "a one-grid-quantum square at the world edge should triangulate");
  require(triangulatedArea2(arrangement) == 2,
          "triangulation should preserve the one-grid-quantum square area");
}

void touchingFaceHolesAreTriangulatedAsTheirUnion() {
  ArrangementResult arrangement;
  // This is the topology around the Tunnels Drill pit: one excluded region is
  // represented by nine child cycles which share Arrangement edges.
  arrangement.vertices = {
      {0, 196000},        {0, 220000},        {-68000, 220000},
      {-68000, 228000},   {-92000, 228000},   {-92000, 220000},
      {-164000, 220000},  {-164000, 256000},  {-188000, 256000},
      {-188000, 220000},  {-220000, 220000},  {-220000, 92000},
      {-256000, 92000},   {-256000, 68000},   {-220000, 68000},
      {-220000, 36000},   {-92000, 36000},    {-92000, 0},
      {-68000, 0},        {-68000, 36000},    {-36000, 36000},
      {-36000, 196000},   {-196000, 196000},  {-196000, 159177},
      {-174740, 196000},  {-196000, 96823},   {-174740, 60000},
      {-81260, 60000},    {-60000, 96823},    {-60000, 159177},
      {-81260, 196000},   {-214000, 128000},  {-196000, 60000},
      {-171000, 53522},   {-85000, 53522},    {-60000, 60000},
      {-42000, 128000},   {-60000, 196000},   {-85000, 202478},
      {-171000, 202478}};
  ArrangementFace face{
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
       18, 19, 20, 21},
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
       18, 19, 20, 21},
      {{307, 327, 318},
       {308, 324, 311, 320, 314, 331, 317, 327},
       {308, 326, 325},
       {309, 310, 324},
       {311, 323, 322, 321},
       {312, 313, 320},
       {314, 319, 332},
       {315, 316, 331},
       {317, 330, 329, 328}},
      {{22, 23, 24},
       {23, 25, 26, 27, 28, 29, 30, 24},
       {25, 23, 31},
       {25, 32, 26},
       {27, 26, 33, 34},
       {27, 35, 28},
       {29, 28, 36},
       {29, 37, 30},
       {24, 30, 38, 39}},
      Membership(0),
      true};
  arrangement.faces.emplace_back(
      ArrangementFace{{}, {}, {}, {}, Membership(0)});
  arrangement.faces.emplace_back(std::move(face));
  arrangement.palette.emplace_back();

  auto const& triangulatedFace = arrangement.faces[1];
  auto expectedArea2 =
      boundaryArea2(arrangement, triangulatedFace.outerBoundaryVertices);
  for (auto const& hole : triangulatedFace.innerBoundaryVertices) {
    expectedArea2 -= boundaryArea2(arrangement, hole);
  }
  require(triangulatedArea2(arrangement) == expectedArea2,
          "touching Face holes emitted triangles across their union");
}
}  // namespace

int main() {
  try {
    triangulatesArrangementAtFixedPointPrecision();
    touchingFaceHolesAreTriangulatedAsTheirUnion();
    std::cout << "Arrangement triangulation preserves fixed-point precision "
                 "and touching holes\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
