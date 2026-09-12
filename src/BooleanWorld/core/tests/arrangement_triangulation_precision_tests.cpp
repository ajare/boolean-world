#include <array>
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

void gridTouchingFaceHolesPreserveArea() {
  constexpr uint32_t size = 3;
  for (uint32_t mask = 1; mask < (1u << (size * size)); ++mask) {
    // A Face has one outer boundary. Omit masks that enclose an unselected
    // island disconnected even at vertices, which belongs to another Face.
    std::array<bool, size * size> exterior{};
    for (uint32_t cell = 0; cell < size * size; ++cell) {
      auto const x = cell % size;
      auto const y = cell / size;
      exterior[cell] = (mask & (1u << cell)) == 0 &&
                       (x == 0 || y == 0 || x + 1 == size || y + 1 == size);
    }
    bool changed = true;
    while (changed) {
      changed = false;
      for (uint32_t cell = 0; cell < size * size; ++cell) {
        if (exterior[cell] || (mask & (1u << cell)) != 0) continue;
        auto const x = cell % size;
        auto const y = cell / size;
        bool connected = false;
        for (int32_t dy = -1; dy <= 1; ++dy) {
          for (int32_t dx = -1; dx <= 1; ++dx) {
            auto const nx = static_cast<int32_t>(x) + dx;
            auto const ny = static_cast<int32_t>(y) + dy;
            if (nx >= 0 && nx < static_cast<int32_t>(size) && ny >= 0 &&
                ny < static_cast<int32_t>(size) &&
                exterior[static_cast<uint32_t>(ny) * size +
                         static_cast<uint32_t>(nx)]) {
              connected = true;
            }
          }
        }
        if (connected) {
          exterior[cell] = true;
          changed = true;
        }
      }
    }
    bool hasEnclosedIsland = false;
    for (uint32_t cell = 0; cell < size * size; ++cell) {
      hasEnclosedIsland |=
          (mask & (1u << cell)) == 0 && !exterior[cell];
    }
    if (hasEnclosedIsland) continue;

    ArrangementResult arrangement;
    arrangement.vertices = {{-1, -1}, {4, -1}, {4, 4}, {-1, 4}};
    for (uint32_t y = 0; y <= size; ++y) {
      for (uint32_t x = 0; x <= size; ++x) {
        arrangement.vertices.push_back(
            {static_cast<int64_t>(x), static_cast<int64_t>(y)});
      }
    }
    ArrangementFace face{{0, 1, 2, 3},
                         {0, 1, 2, 3},
                         {},
                         {},
                         Membership(0),
                         true};
    auto vertex = [](uint32_t x, uint32_t y) {
      return 4 + y * (size + 1) + x;
    };
    auto horizontal = [](uint32_t x, uint32_t y) {
      return 4 + y * size + x;
    };
    auto vertical = [](uint32_t x, uint32_t y) {
      return 4 + size * (size + 1) + y * (size + 1) + x;
    };
    uint32_t cells = 0;
    for (uint32_t y = 0; y < size; ++y) {
      for (uint32_t x = 0; x < size; ++x) {
        if ((mask & (1u << (y * size + x))) == 0) continue;
        ++cells;
        face.innerBoundaryVertices.push_back(
            {vertex(x, y), vertex(x, y + 1), vertex(x + 1, y + 1),
             vertex(x + 1, y)});
        face.innerBoundaries.push_back(
            {vertical(x, y), horizontal(x, y + 1), vertical(x + 1, y),
             horizontal(x, y)});
      }
    }
    arrangement.faces.emplace_back(
        ArrangementFace{{}, {}, {}, {}, Membership(0)});
    arrangement.faces.push_back(std::move(face));
    arrangement.palette.emplace_back();
    auto const expectedArea2 = int64_t{50 - cells * 2};
    require(triangulatedArea2(arrangement) == expectedArea2,
            "grid-touching Face holes changed triangulation area for mask " +
                std::to_string(mask));
  }
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
    gridTouchingFaceHolesPreserveArea();
    std::cout << "Arrangement triangulation preserves fixed-point precision "
                 "and touching holes\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
