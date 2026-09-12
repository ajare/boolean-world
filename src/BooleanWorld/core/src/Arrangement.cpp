#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <format>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <mapbox/earcut.hpp>

#include <core/Arrangement.h>
#include <core/CoreException.h>
#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/Timer.h>

namespace bw::core::arr {
using namespace std;

Membership::Membership(size_t primitiveCount)
    : mWords((primitiveCount + 63) / 64) {
}

void Membership::set(size_t primitiveIndex, bool value) {
  auto wordIndex = primitiveIndex / 64;
  auto mask = uint64_t(1) << (primitiveIndex % 64);
  if (value) {
    mWords[wordIndex] |= mask;
  } else {
    mWords[wordIndex] &= ~mask;
  }
}

bool Membership::contains(size_t primitiveIndex) const {
  auto wordIndex = primitiveIndex / 64;
  return wordIndex < mWords.size() &&
         (mWords[wordIndex] & (uint64_t(1) << (primitiveIndex % 64))) != 0;
}

static bool ApplyOperation(
    bw::core::Primitive::Operation operation,
    bool accumulated,
    bool member) {
  switch (operation) {
    case bw::core::Primitive::Operation::Union:
      return accumulated || member;
    case bw::core::Primitive::Operation::Intersection:
      return accumulated && member;
    case bw::core::Primitive::Operation::Difference:
      return accumulated && !member;
    case bw::core::Primitive::Operation::XOR:
      return accumulated != member;
  }
  return accumulated;
}

PrimitiveFoldOrder BuildPrimitiveFoldOrder(
    vector<ArrangementPrimitive> const& primitives) {
  PrimitiveFoldOrder foldOrder(primitives.size());
  for (uint32_t primitiveIndex = 0;
       primitiveIndex < uint32_t(foldOrder.size()); ++primitiveIndex) {
    foldOrder[primitiveIndex] = primitiveIndex;
  }
  stable_sort(
      foldOrder.begin(), foldOrder.end(), [&](uint32_t lhs, uint32_t rhs) {
        return primitives[lhs].priority < primitives[rhs].priority;
      });
  return foldOrder;
}

bool EvaluateFold(
    vector<ArrangementPrimitive> const& primitives,
    Membership const& membership,
    PrimitiveFoldOrder const& foldOrder) {
  bool inside = false;
  for (auto primitiveIndex : foldOrder) {
    inside = ApplyOperation(
        primitives[primitiveIndex].operation,
        inside,
        membership.contains(primitiveIndex));
  }
  return inside;
}

namespace {
struct Segment {
  FixedPointVertex v[2];
  uint32_t primitiveIndex;
  std::optional<bool> collidesOverride;
  std::optional<bool> visibleOverride;
  std::optional<WallNormalMapOverride> normalMapOverride;
  std::optional<WallMaskOverride> wallMaskOverride;
  bool contributesProperties;
};

struct RationalPoint {
  int64_t xNumerator;
  int64_t yNumerator;
  int64_t denominator;
};

struct Box {
  int64_t minx;
  int64_t miny;
  int64_t maxx;
  int64_t maxy;
};

struct PointHash {
  size_t operator()(FixedPointVertex const& v) const {
    auto x = hash<int64_t>()(v.x);
    auto y = hash<int64_t>()(v.y);
    return x ^ (y + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2));
  }
};

int64_t CrossVector(int64_t ax, int64_t ay, int64_t bx, int64_t by) {
  return ax * by - ay * bx;
}

int64_t Cross(FixedPointVertex const& a, FixedPointVertex const& b, FixedPointVertex const& c) {
  return CrossVector(b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y);
}

int DifferenceOfProductsSign(int64_t a, int64_t b, int64_t c, int64_t d) {
#ifdef _MSC_VER
  int64_t lhsHigh;
  int64_t rhsHigh;
  auto lhsLow = uint64_t(_mul128(a, b, &lhsHigh));
  auto rhsLow = uint64_t(_mul128(c, d, &rhsHigh));
  auto low = lhsLow - rhsLow;
  auto high = uint64_t(lhsHigh) - uint64_t(rhsHigh) - uint64_t(lhsLow < rhsLow);
  return (high >> 63) != 0 ? -1 : (high > 0 || low > 0 ? 1 : 0);
#else
  using int128 = __int128;
  auto result = int128(a) * int128(b) - int128(c) * int128(d);
  return (result > 0) - (result < 0);
#endif
}

int CrossSign(FixedPointVertex const& a, FixedPointVertex const& b, RationalPoint const& point) {
  auto relativeX = point.xNumerator - a.x * point.denominator;
  auto relativeY = point.yNumerator - a.y * point.denominator;
  return DifferenceOfProductsSign(
      b.x - a.x,
      relativeY,
      b.y - a.y,
      relativeX);
}

uint64_t UnsignedMagnitude(int64_t value) {
  return value < 0 ? uint64_t(-(value + 1)) + 1 : uint64_t(value);
}

// Returns round(base + numerator * multiplier / denominator) without
// overflowing a 64-bit intermediate. Half-grid ties are rounded away from
// zero, independently of segment direction or pair ordering.
int64_t RoundedCoordinate(int64_t base, int64_t numerator, int64_t multiplier, int64_t denominator) {
  if (denominator < 0) {
    numerator = -numerator;
    denominator = -denominator;
  }

  int64_t quotient;
  int64_t remainder;
#ifdef _MSC_VER
  int64_t high;
  auto low = _mul128(numerator, multiplier, &high);
  quotient = _div128(high, low, denominator, &remainder);
#else
  using int128 = __int128;
  auto product = int128(numerator) * int128(multiplier);
  quotient = int64_t(product / denominator);
  remainder = int64_t(product % denominator);
#endif

  auto result = base + quotient;
  auto remainderMagnitude = UnsignedMagnitude(remainder);
  auto denominatorMagnitude = uint64_t(denominator);
  auto beyondHalf = remainderMagnitude > denominatorMagnitude - remainderMagnitude;
  auto atHalf = remainderMagnitude == denominatorMagnitude - remainderMagnitude;
  auto remainderSign = (remainder > 0) - (remainder < 0);

  if (beyondHalf ||
      (atHalf && (result == 0 || (result > 0) == (remainder > 0)))) {
    result += remainderSign;
  }
  return result;
}

vector<Segment> ExtractSegments(vector<ContourInput> const& contours) {
  vector<Segment> result;

  for (auto const& input : contours) {
    auto const& contour = input.contour;
    if (contour.size() < 2) {
      continue;
    }

    for (size_t i = 0; i < contour.size(); ++i) {
      auto j = (i + 1) % contour.size();
      auto const& a = contour[i];
      auto const& b = contour[j];
      if (a != b) {
        std::optional<bool> collidesOverride =
            i < input.edgeOverrides.size() ? input.edgeOverrides[i] : std::nullopt;
        std::optional<bool> visibleOverride =
            i < input.edgeVisibleOverrides.size() ? input.edgeVisibleOverrides[i] : std::nullopt;
        std::optional<WallNormalMapOverride> normalMapOverride =
            i < input.edgeNormalMapOverrides.size()
                ? input.edgeNormalMapOverrides[i]
                : std::nullopt;
        std::optional<WallMaskOverride> wallMaskOverride =
            i < input.edgeWallMaskOverrides.size()
                ? input.edgeWallMaskOverrides[i]
                : std::nullopt;
        result.push_back(
            {{a, b}, input.primitiveIndex, collidesOverride, visibleOverride, normalMapOverride, wallMaskOverride, input.contributesProperties});
      }
    }
  }

  return result;
}

bool PointOnSegment(FixedPointVertex const& point, FixedPointVertex const& a, FixedPointVertex const& b) {
  return Cross(a, b, point) == 0 &&
         point.x >= min(a.x, b.x) && point.x <= max(a.x, b.x) &&
         point.y >= min(a.y, b.y) && point.y <= max(a.y, b.y);
}

struct Intersection {
  bool hit{false};
  FixedPointVertex point{};
};

Intersection SegmentIntersection(Segment const& subject, Segment const& clip) {
  auto const& p = subject.v[0];
  auto const& q = clip.v[0];
  auto rx = subject.v[1].x - p.x;
  auto ry = subject.v[1].y - p.y;
  auto sx = clip.v[1].x - q.x;
  auto sy = clip.v[1].y - q.y;
  auto denominator = CrossVector(rx, ry, sx, sy);

  if (denominator == 0) {
    return {};
  }

  auto qpx = q.x - p.x;
  auto qpy = q.y - p.y;
  auto subjectNumerator = CrossVector(qpx, qpy, sx, sy);
  auto clipNumerator = CrossVector(qpx, qpy, rx, ry);

  if (denominator > 0) {
    if (subjectNumerator < 0 || subjectNumerator > denominator ||
        clipNumerator < 0 || clipNumerator > denominator) {
      return {};
    }
  } else if (subjectNumerator > 0 || subjectNumerator < denominator ||
             clipNumerator > 0 || clipNumerator < denominator) {
    return {};
  }

  return {
      true,
      {RoundedCoordinate(p.x, subjectNumerator, rx, denominator),
       RoundedCoordinate(p.y, subjectNumerator, ry, denominator)}};
}

bool FixedPointVertexLessAlongSegment(FixedPointVertex const& lhs, FixedPointVertex const& rhs, Segment const& segment) {
  auto dx = segment.v[1].x - segment.v[0].x;
  auto dy = segment.v[1].y - segment.v[0].y;
  if (abs(dx) >= abs(dy)) {
    if (lhs.x != rhs.x) {
      return dx >= 0 ? lhs.x < rhs.x : lhs.x > rhs.x;
    }
    return dy >= 0 ? lhs.y < rhs.y : lhs.y > rhs.y;
  }
  if (lhs.y != rhs.y) {
    return dy >= 0 ? lhs.y < rhs.y : lhs.y > rhs.y;
  }
  return dx >= 0 ? lhs.x < rhs.x : lhs.x > rhs.x;
}

int64_t SignedArea2(PSLG const& graph, vector<int> const& vertices) {
  int64_t area = 0;
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto const& a = graph.vs[vertices[i]];
    auto const& b = graph.vs[vertices[(i + 1) % vertices.size()]];
    area += a.x * b.y - a.y * b.x;
  }
  return area;
}

bool PointInClosedTriangle(
    FixedPointVertex const& point,
    FixedPointVertex const& a,
    FixedPointVertex const& b,
    FixedPointVertex const& c) {
  return Cross(a, b, point) >= 0 && Cross(b, c, point) >= 0 &&
         Cross(c, a, point) >= 0;
}

vector<array<int, 3>> TriangulateCycle(PSLG const& graph, Cycle const& cycle) {
  vector<int> remaining = cycle.vis;
  bool removedCollinearVertex = true;
  while (removedCollinearVertex && remaining.size() > 3) {
    removedCollinearVertex = false;
    for (size_t i = 0; i < remaining.size(); ++i) {
      auto const& previous = graph.vs[remaining[(i + remaining.size() - 1) % remaining.size()]];
      auto const& current = graph.vs[remaining[i]];
      auto const& next = graph.vs[remaining[(i + 1) % remaining.size()]];
      if (Cross(previous, current, next) == 0) {
        remaining.erase(remaining.begin() + i);
        removedCollinearVertex = true;
        break;
      }
    }
  }

  vector<array<int, 3>> triangles;
  while (remaining.size() > 3) {
    bool removedEar = false;
    for (size_t i = 0; i < remaining.size(); ++i) {
      array<int, 3> triangle{
          remaining[(i + remaining.size() - 1) % remaining.size()],
          remaining[i],
          remaining[(i + 1) % remaining.size()]};
      auto const& a = graph.vs[triangle[0]];
      auto const& b = graph.vs[triangle[1]];
      auto const& c = graph.vs[triangle[2]];
      if (Cross(a, b, c) <= 0) {
        continue;
      }

      auto containsVertex = false;
      for (auto vertexIndex : remaining) {
        if (vertexIndex == triangle[0] || vertexIndex == triangle[1] ||
            vertexIndex == triangle[2]) {
          continue;
        }
        if (PointInClosedTriangle(graph.vs[vertexIndex], a, b, c)) {
          containsVertex = true;
          break;
        }
      }
      if (containsVertex) {
        continue;
      }

      triangles.push_back(triangle);
      remaining.erase(remaining.begin() + i);
      removedEar = true;
      break;
    }

    // Minimal arrangement cycles are simple, so the ear theorem guarantees
    // progress after collinear vertices are removed.
    if (!removedEar) {
      break;
    }
  }

  if (remaining.size() == 3 &&
      Cross(graph.vs[remaining[0]], graph.vs[remaining[1]], graph.vs[remaining[2]]) > 0) {
    triangles.push_back({remaining[0], remaining[1], remaining[2]});
  }
  return triangles;
}

RationalPoint TriangleCentroid(PSLG const& graph, array<int, 3> const& triangle) {
  auto const& a = graph.vs[triangle[0]];
  auto const& b = graph.vs[triangle[1]];
  auto const& c = graph.vs[triangle[2]];
  return {a.x + b.x + c.x, a.y + b.y + c.y, 3};
}

RationalPoint SamplePoint(PSLG const& graph, Cycle const& cycle) {
  // A convex ear is wholly inside this counter-clockwise cycle, so its
  // centroid is a strict interior point. Keep the largest ear from an exact
  // integer triangulation to stay well clear of the boundary even for slivers.
  auto triangles = TriangulateCycle(graph, cycle);
  auto largest = max_element(
      triangles.begin(), triangles.end(), [&](auto const& lhs, auto const& rhs) {
        return Cross(graph.vs[lhs[0]], graph.vs[lhs[1]], graph.vs[lhs[2]]) <
               Cross(graph.vs[rhs[0]], graph.vs[rhs[1]], graph.vs[rhs[2]]);
      });
  if (largest == triangles.end()) {
    throw logic_error("Cannot sample a zero-area arrangement cycle.");
  }
  return TriangleCentroid(graph, *largest);
}

int PointInCycle(RationalPoint const& point, Cycle const& cycle, PSLG const& graph) {
  int winding = 0;
  for (size_t i = 0; i < cycle.vis.size(); ++i) {
    auto const& a = graph.vs[cycle.vis[i]];
    auto const& b = graph.vs[cycle.vis[(i + 1) % cycle.vis.size()]];
    auto aBelow = a.y * point.denominator <= point.yNumerator;
    auto bBelow = b.y * point.denominator <= point.yNumerator;
    auto cross = CrossSign(a, b, point);

    if (aBelow) {
      if (!bBelow && cross > 0) {
        ++winding;
      }
    } else if (bBelow && cross < 0) {
      --winding;
    }
  }
  return winding == 0 ? 0 : 1;
}

RationalPoint SamplePoint(
    PSLG const& graph,
    Face const& face,
    vector<Cycle> const& cycles) {
  auto const& cycle = cycles[face.polygon];
  auto triangles = TriangulateCycle(graph, cycle);
  array<int, 3> const* largestTriangle = nullptr;
  int64_t largestArea = 0;
  for (auto const& triangle : triangles) {
    auto centroid = TriangleCentroid(graph, triangle);
    auto insideHole = any_of(
        face.holes.begin(), face.holes.end(), [&](auto hole) {
          return PointInCycle(centroid, cycles[hole], graph) != 0;
        });
    auto area = Cross(
        graph.vs[triangle[0]],
        graph.vs[triangle[1]],
        graph.vs[triangle[2]]);
    if (!insideHole && area > largestArea) {
      largestArea = area;
      largestTriangle = &triangle;
    }
  }

  if (largestTriangle) {
    return TriangleCentroid(graph, *largestTriangle);
  }

  // A large hole can contain every ear centroid even though a positive-area
  // strip remains between it and the outer boundary. Approach an original
  // boundary-edge midpoint from inside one of its ears until the point clears
  // every hole. Keeping the point rational avoids losing a narrow face to
  // fixed-point rounding.
  auto isBoundaryEdge = [&](int first, int second) {
    for (size_t i = 0; i < cycle.vis.size(); ++i) {
      auto a = cycle.vis[i];
      auto b = cycle.vis[(i + 1) % cycle.vis.size()];
      if ((a == first && b == second) || (a == second && b == first)) {
        return true;
      }
    }
    return false;
  };
  auto outsideHoles = [&](RationalPoint const& point) {
    return all_of(face.holes.begin(), face.holes.end(), [&](auto hole) {
      return PointInCycle(point, cycles[hole], graph) == 0;
    });
  };

  for (auto const& triangle : triangles) {
    for (size_t edge = 0; edge < 3; ++edge) {
      auto first = triangle[edge];
      auto second = triangle[(edge + 1) % 3];
      if (!isBoundaryEdge(first, second)) {
        continue;
      }
      auto opposite = triangle[(edge + 2) % 3];
      auto const& a = graph.vs[first];
      auto const& b = graph.vs[second];
      auto const& c = graph.vs[opposite];
      for (int64_t edgeWeight = 1; edgeWeight <= (int64_t{1} << 30);
           edgeWeight *= 2) {
        RationalPoint point{
            edgeWeight * (a.x + b.x) + c.x,
            edgeWeight * (a.y + b.y) + c.y,
            edgeWeight * 2 + 1};
        if (outsideHoles(point)) {
          return point;
        }
      }
    }
  }

  throw logic_error("Cannot find an interior sample for an arrangement face.");
}

int PointInCycle(FixedPointVertex const& point, Cycle const& cycle, PSLG const& graph) {
  int winding = 0;
  for (size_t i = 0; i < cycle.vis.size(); ++i) {
    auto const& a = graph.vs[cycle.vis[i]];
    auto const& b = graph.vs[cycle.vis[(i + 1) % cycle.vis.size()]];
    if (PointOnSegment(point, a, b)) {
      return -1;
    }
    auto cross = Cross(a, b, point);
    if (a.y <= point.y) {
      if (b.y > point.y && cross > 0) {
        ++winding;
      }
    } else if (b.y <= point.y && cross < 0) {
      --winding;
    }
  }
  return winding == 0 ? 0 : 1;
}

int ContourWinding(RationalPoint const& point, Contour const& contour) {
  int winding = 0;
  for (size_t i = 0; i < contour.size(); ++i) {
    auto const& a = contour[i];
    auto const& b = contour[(i + 1) % contour.size()];
    auto aBelow = a.y * point.denominator <= point.yNumerator;
    auto bBelow = b.y * point.denominator <= point.yNumerator;
    auto cross = CrossSign(a, b, point);
    if (aBelow) {
      if (!bBelow && cross > 0) {
        ++winding;
      }
    } else if (bBelow && cross < 0) {
      --winding;
    }
  }
  return winding;
}

Box GetBounds(PSLG const& graph, Cycle const& cycle) {
  auto const& first = graph.vs[cycle.vis[0]];
  Box bounds{first.x, first.y, first.x, first.y};
  for (auto vertexIndex : cycle.vis) {
    auto const& vertex = graph.vs[vertexIndex];
    bounds.minx = min(bounds.minx, vertex.x);
    bounds.miny = min(bounds.miny, vertex.y);
    bounds.maxx = max(bounds.maxx, vertex.x);
    bounds.maxy = max(bounds.maxy, vertex.y);
  }
  return bounds;
}

bool ContainsBox(Box const& outer, Box const& inner) {
  return outer.minx <= inner.minx && outer.maxx >= inner.maxx &&
         outer.miny <= inner.miny && outer.maxy >= inner.maxy;
}

struct HierarchyGrid {
  wp::AccelerationGrid grid;

  explicit HierarchyGrid(vector<Box> const& boxes)
      : grid(CreateGrid(boxes)) {
    for (uint32_t i = 0; i < uint32_t(boxes.size()); ++i) {
      auto const& box = boxes[i];
      grid.addItem(
          i,
          wp::BoundingBox(
              float(box.minx), float(box.miny),
              float(box.maxx - box.minx), float(box.maxy - box.miny)));
    }
  }

private:
  static wp::AccelerationGrid CreateGrid(vector<Box> const& boxes) {
    if (boxes.empty()) {
      return wp::AccelerationGrid(0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
    }

    auto minx = boxes[0].minx;
    auto miny = boxes[0].miny;
    auto maxx = boxes[0].maxx;
    auto maxy = boxes[0].maxy;
    for (auto const& box : boxes) {
      minx = min(minx, box.minx);
      miny = min(miny, box.miny);
      maxx = max(maxx, box.maxx);
      maxy = max(maxy, box.maxy);
    }

    auto width = max<int64_t>(1, maxx - minx);
    auto height = max<int64_t>(1, maxy - miny);
    auto targetCells = clamp<size_t>(boxes.size(), 1, 128 * 128);
    auto aspect = double(width) / double(height);
    auto dimX = clamp(
        int(lround(sqrt(double(targetCells) * aspect))), 1, 128);
    auto dimY = clamp(
        int(lround(sqrt(double(targetCells) / aspect))), 1, 128);
    return wp::AccelerationGrid(
        float(minx), float(miny), float(width), float(height), dimX, dimY);
  }
};

struct SegmentGrid {
  wp::AccelerationGrid grid;

  explicit SegmentGrid(vector<Segment> const& segments)
      : grid(CreateGrid(segments)) {
    for (uint32_t i = 0; i < uint32_t(segments.size()); ++i) {
      auto const& segment = segments[i];
      auto minx = min(segment.v[0].x, segment.v[1].x);
      auto miny = min(segment.v[0].y, segment.v[1].y);
      auto maxx = max(segment.v[0].x, segment.v[1].x);
      auto maxy = max(segment.v[0].y, segment.v[1].y);
      grid.addItem(
          i,
          wp::BoundingBox(
              float(minx), float(miny),
              float(maxx - minx), float(maxy - miny)));
    }
  }

private:
  static wp::AccelerationGrid CreateGrid(vector<Segment> const& segments) {
    if (segments.empty()) {
      return wp::AccelerationGrid(0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
    }

    auto minx = segments[0].v[0].x;
    auto miny = segments[0].v[0].y;
    auto maxx = minx;
    auto maxy = miny;
    for (auto const& segment : segments) {
      for (auto const& vertex : segment.v) {
        minx = min(minx, vertex.x);
        miny = min(miny, vertex.y);
        maxx = max(maxx, vertex.x);
        maxy = max(maxy, vertex.y);
      }
    }

    auto width = max<int64_t>(1, maxx - minx);
    auto height = max<int64_t>(1, maxy - miny);
    auto targetCells = clamp<size_t>(segments.size(), 1, 128 * 128);
    auto aspect = double(width) / double(height);
    auto dimX = clamp(
        int(lround(sqrt(double(targetCells) * aspect))), 1, 128);
    auto dimY = clamp(
        int(lround(sqrt(double(targetCells) / aspect))), 1, 128);
    // Keep cells at least one fixed-point quantum wide so half-quantum
    // snap-rounding cannot skip beyond the neighbouring-cell search.
    dimX = int(min<int64_t>(dimX, width));
    dimY = int(min<int64_t>(dimY, height));
    return wp::AccelerationGrid(
        float(minx), float(miny), float(width), float(height), dimX, dimY);
  }
};
}  // namespace

PSLG BuildPSLG(
    vector<ContourInput> const& contours,
    PSLGConstructionStats* stats) {
  auto segments = ExtractSegments(contours);
  vector<vector<FixedPointVertex>> splits(segments.size());
  vector<FixedPointVertex> candidates;

  for (size_t i = 0; i < segments.size(); ++i) {
    splits[i].push_back(segments[i].v[0]);
    splits[i].push_back(segments[i].v[1]);
    candidates.push_back(segments[i].v[0]);
    candidates.push_back(segments[i].v[1]);
  }

  SegmentGrid broadPhase(segments);
  vector<size_t> visited(segments.size(), numeric_limits<size_t>::max());
  uint64_t candidateSegmentPairTests = 0;

  // Segment bounding boxes are registered in every cell they touch. Walking
  // each segment's cells and using a per-segment marker tests every possible
  // intersection once without materialising the quadratic pair set.
  for (size_t i = 0; i < segments.size(); ++i) {
    for (auto cellIndex : broadPhase.grid._getItemCellIndices(uint32_t(i))) {
      auto cellX = int(cellIndex % broadPhase.grid.getCellDimensionX());
      auto cellY = int(cellIndex / broadPhase.grid.getCellDimensionX());
      for (auto j : broadPhase.grid._getCellItems(cellX, cellY)) {
        if (j <= i || visited[j] == i) {
          continue;
        }
        visited[j] = i;
        ++candidateSegmentPairTests;

        auto intersection = SegmentIntersection(segments[i], segments[j]);
        if (!intersection.hit) {
          continue;
        }
        splits[i].push_back(intersection.point);
        splits[j].push_back(intersection.point);
        candidates.push_back(intersection.point);
      }
    }
  }

  auto candidatePointCount = candidates.size();
  unordered_set<FixedPointVertex, PointHash> uniqueCandidates;
  uniqueCandidates.reserve(candidates.size());
  uniqueCandidates.insert(candidates.begin(), candidates.end());

  uint64_t candidatePointSegmentTests = 0;
  fill(visited.begin(), visited.end(), numeric_limits<size_t>::max());
  size_t candidateIndex = 0;

  // Snapping can create an incidence on an edge that was not in the original
  // intersection pair. A point can move by at most half a grid quantum, so
  // its cell and eight neighbours conservatively contain every affected edge.
  for (auto const& candidate : uniqueCandidates) {
    int containingX;
    int containingY;
    broadPhase.grid.getContainingCell(
        true, float(candidate.x), float(candidate.y),
        containingX, containingY);

    auto minX = max(0, containingX - 1);
    auto minY = max(0, containingY - 1);
    auto maxX = min(broadPhase.grid.getCellDimensionX() - 1, containingX + 1);
    auto maxY = min(broadPhase.grid.getCellDimensionY() - 1, containingY + 1);
    for (int y = minY; y <= maxY; ++y) {
      for (int x = minX; x <= maxX; ++x) {
        for (auto segmentIndex : broadPhase.grid._getCellItems(x, y)) {
          if (visited[segmentIndex] == candidateIndex) {
            continue;
          }
          visited[segmentIndex] = candidateIndex;
          ++candidatePointSegmentTests;
          if (PointOnSegment(
                  candidate,
                  segments[segmentIndex].v[0],
                  segments[segmentIndex].v[1])) {
            splits[segmentIndex].push_back(candidate);
          }
        }
      }
    }
    ++candidateIndex;
  }

  if (stats != nullptr) {
    stats->segmentCount = segments.size();
    stats->exhaustiveSegmentPairTests =
        uint64_t(segments.size()) * uint64_t(segments.size() - (segments.empty() ? 0 : 1)) / 2;
    stats->candidateSegmentPairTests = candidateSegmentPairTests;
    stats->candidatePointCount = candidatePointCount;
    stats->uniqueCandidatePointCount = uniqueCandidates.size();
    stats->exhaustivePointSegmentTests =
        uint64_t(candidatePointCount) * uint64_t(segments.size());
    stats->candidatePointSegmentTests = candidatePointSegmentTests;
  }

  PSLG graph;
  unordered_map<FixedPointVertex, int, PointHash> vertexMap;
  map<pair<int, int>, int> edgeMap;

  auto getFixedPointVertex = [&](FixedPointVertex const& vertex) {
    auto [it, inserted] = vertexMap.emplace(vertex, int(graph.vs.size()));
    if (inserted) {
      graph.vs.push_back(vertex);
    }
    return it->second;
  };

  for (size_t i = 0; i < segments.size(); ++i) {
    auto& points = splits[i];
    sort(points.begin(), points.end(), [&](FixedPointVertex const& lhs, FixedPointVertex const& rhs) {
      return FixedPointVertexLessAlongSegment(lhs, rhs, segments[i]);
    });
    points.erase(unique(points.begin(), points.end()), points.end());

    for (size_t j = 0; j + 1 < points.size(); ++j) {
      if (points[j] == points[j + 1]) {
        continue;
      }
      auto directedStart = getFixedPointVertex(points[j]);
      auto directedEnd = getFixedPointVertex(points[j + 1]);
      auto a = directedStart;
      auto b = directedEnd;
      if (a > b) {
        swap(a, b);
      }

      auto [edgeIt, inserted] = edgeMap.emplace(
          make_pair(a, b), int(graph.es.size()));
      if (inserted) {
        graph.es.push_back({a, b});
      }

      if (segments[i].primitiveIndex != ~0u) {
        auto& edge = graph.es[edgeIt->second];
        auto contribution = find_if(
            edge.windingDeltas.begin(), edge.windingDeltas.end(),
            [&](WindingDelta const& value) {
              return value.primitiveIndex == segments[i].primitiveIndex;
            });
        auto delta = directedStart == edge.vi[0] ? 1 : -1;
        if (contribution == edge.windingDeltas.end()) {
          edge.windingDeltas.push_back({segments[i].primitiveIndex, delta});
        } else {
          contribution->delta += delta;
        }

        if (segments[i].collidesOverride.has_value()) {
          // Unset contributors have no effect; among authored overrides,
          // "doesn't collide" dominates "collides" independent of source
          // Primitive or contribution order.
          edge.collidesOverride = edge.collidesOverride.has_value()
                                      ? *edge.collidesOverride && *segments[i].collidesOverride
                                      : segments[i].collidesOverride;
        }
        if (segments[i].visibleOverride.has_value() && !edge.visibleOverride.has_value()) {
          edge.visibleOverride = segments[i].visibleOverride;
        }
        // Inputs are in fold order, so every explicit later property-
        // contributing Primitive has higher precedence. An Unset value is not
        // a choice: it must leave a lower Image or Disabled value intact.
        if (segments[i].contributesProperties &&
            segments[i].normalMapOverride.has_value() &&
            segments[i].normalMapOverride->state() !=
                WallNormalMapOverride::State::Unset) {
          edge.normalMapOverride = segments[i].normalMapOverride;
        }
        // The Wall mask resolves with the exact same precedence as the Wall
        // normal map: Disabled dominates lower contributors, Unset is inert,
        // and the highest-precedence Image wins as one complete value.
        if (segments[i].contributesProperties &&
            segments[i].wallMaskOverride.has_value() &&
            segments[i].wallMaskOverride->state() !=
                WallMaskOverride::State::Unset) {
          edge.wallMaskOverride = segments[i].wallMaskOverride;
        }
      }
    }
  }

  return graph;
}

vector<Cycle> ExtractMinimalCycles(PSLG const& graph) {
  struct HalfEdge {
    int v[2];
    int twin;
    int edge;
    int next{-1};
    bool visited{false};
  };

  vector<HalfEdge> halfEdges;
  vector<vector<int>> outgoing(graph.vs.size());
  halfEdges.reserve(graph.es.size() * 2);

  for (int i = 0; i < int(graph.es.size()); ++i) {
    auto const& edge = graph.es[i];
    auto h = int(halfEdges.size());
    halfEdges.push_back({{edge.vi[0], edge.vi[1]}, h + 1, i});
    halfEdges.push_back({{edge.vi[1], edge.vi[0]}, h, i});
    outgoing[edge.vi[0]].push_back(h);
    outgoing[edge.vi[1]].push_back(h + 1);
  }

  // Exact polar ordering keeps face adjacency independent of floating point.
  for (auto& edges : outgoing) {
    sort(edges.begin(), edges.end(), [&](int lhsIndex, int rhsIndex) {
      auto const& lhs = halfEdges[lhsIndex];
      auto const& rhs = halfEdges[rhsIndex];
      auto const& origin = graph.vs[lhs.v[0]];
      auto const& lhsEnd = graph.vs[lhs.v[1]];
      auto const& rhsEnd = graph.vs[rhs.v[1]];
      auto ldx = lhsEnd.x - origin.x;
      auto ldy = lhsEnd.y - origin.y;
      auto rdx = rhsEnd.x - origin.x;
      auto rdy = rhsEnd.y - origin.y;
      auto lhsUpper = ldy > 0 || (ldy == 0 && ldx >= 0);
      auto rhsUpper = rdy > 0 || (rdy == 0 && rdx >= 0);
      if (lhsUpper != rhsUpper) {
        return lhsUpper > rhsUpper;
      }
      auto cross = CrossVector(ldx, ldy, rdx, rdy);
      if (cross != 0) {
        return cross > 0;
      }
      return lhs.edge < rhs.edge;
    });
  }

  for (auto& halfEdge : halfEdges) {
    auto& edges = outgoing[halfEdges[halfEdge.twin].v[0]];
    auto twin = find(edges.begin(), edges.end(), halfEdge.twin);
    auto index = int(distance(edges.begin(), twin));
    halfEdge.next = edges[(index - 1 + int(edges.size())) % int(edges.size())];
  }

  vector<Cycle> cycles;
  for (int start = 0; start < int(halfEdges.size()); ++start) {
    if (halfEdges[start].visited) {
      continue;
    }

    Cycle cycle;
    auto current = start;
    do {
      halfEdges[current].visited = true;
      cycle.vis.push_back(halfEdges[current].v[0]);
      cycle.eis.push_back(halfEdges[current].edge);
      current = halfEdges[current].next;
    } while (current != start);

    if (cycle.vis.size() < 3) {
      continue;
    }
    cycle.area = SignedArea2(graph, cycle.vis);
    // Zero-area walks are construction debris. Clockwise walks bound the
    // unbounded exterior rather than a bounded arrangement face.
    if (cycle.area > 0) {
      cycles.push_back(move(cycle));
    }
  }

  return cycles;
}

vector<PolygonNode> BuildPolygonHierarchy(
    PSLG const& graph,
    vector<Cycle> const& cycles,
    PolygonHierarchyStats* stats) {
  if (stats != nullptr) {
    *stats = {};
  }

  vector<PolygonNode> nodes(cycles.size());
  vector<Box> boxes(cycles.size());
  for (int i = 0; i < int(cycles.size()); ++i) {
    nodes[i].cycleIndex = i;
    boxes[i] = GetBounds(graph, cycles[i]);
  }

  HierarchyGrid broadPhase(boxes);
  wp::AccelerationGrid::IndexCollection candidates;
  for (int i = 0; i < int(cycles.size()); ++i) {
    auto const& box = boxes[i];
    // Every containing box covers the child's minimum corner. Querying that
    // one grid cell avoids returning cycles that merely overlap the child.
    broadPhase.grid.getCandidateItemsInBoundingArea(
        wp::BoundingBox(float(box.minx), float(box.miny), 0.0f, 0.0f),
        candidates);
    sort(candidates.begin(), candidates.end(), [&](uint32_t lhs, uint32_t rhs) {
      auto lhsArea = abs(cycles[lhs].area);
      auto rhsArea = abs(cycles[rhs].area);
      return lhsArea != rhsArea ? lhsArea < rhsArea : lhs < rhs;
    });

    auto sample = SamplePoint(graph, cycles[i]);
    auto bestParent = -1;
    for (auto candidate : candidates) {
      if (stats != nullptr) {
        ++stats->indexedCandidateBoxTests;
      }
      if (candidate == uint32_t(i) ||
          !ContainsBox(boxes[candidate], box)) {
        continue;
      }
      if (stats != nullptr) {
        ++stats->pointInCycleTests;
      }
      if (PointInCycle(sample, cycles[candidate], graph)) {
        bestParent = int(candidate);
        break;
      }
    }
    nodes[i].parent = bestParent;
  }

  if (stats != nullptr) {
    stats->exhaustiveCandidateBoxTests =
        uint64_t(cycles.size()) * uint64_t(cycles.size() - (cycles.empty() ? 0 : 1));
  }

  for (int i = 0; i < int(nodes.size()); ++i) {
    if (nodes[i].parent >= 0) {
      nodes[nodes[i].parent].children.push_back(i);
    }
  }
  return nodes;
}

vector<Face> BuildFaces(vector<PolygonNode> const& nodes) {
  vector<Face> faces;
  for (auto const& node : nodes) {
    Face face;
    face.polygon = node.cycleIndex;
    for (auto child : node.children) {
      face.holes.push_back(nodes[child].cycleIndex);
    }
    faces.push_back(move(face));
  }
  return faces;
}

ArrangementResultPtr BuildArrangement(
    vector<ArrangementPrimitive> const& primitives,
    ArrangementStats* stats) {
  auto foldOrder = BuildPrimitiveFoldOrder(primitives);

  vector<ContourInput> contours;
  for (auto primitiveIndex : foldOrder) {
    auto const& primitive = primitives[primitiveIndex];
    for (size_t contourIndex = 0; contourIndex < primitive.contours.size(); ++contourIndex) {
      std::vector<std::optional<bool>> edgeOverrides;
      if (contourIndex < primitive.contourEdgeOverrides.size()) {
        edgeOverrides = primitive.contourEdgeOverrides[contourIndex];
      }
      std::vector<std::optional<bool>> edgeVisibleOverrides;
      if (contourIndex < primitive.contourEdgeVisibleOverrides.size()) {
        edgeVisibleOverrides = primitive.contourEdgeVisibleOverrides[contourIndex];
      }
      std::vector<std::optional<WallNormalMapOverride>> edgeNormalMapOverrides;
      if (contourIndex < primitive.contourEdgeNormalMapOverrides.size()) {
        edgeNormalMapOverrides =
            primitive.contourEdgeNormalMapOverrides[contourIndex];
      }
      std::vector<std::optional<WallMaskOverride>> edgeWallMaskOverrides;
      if (contourIndex < primitive.contourEdgeWallMaskOverrides.size()) {
        edgeWallMaskOverrides =
            primitive.contourEdgeWallMaskOverrides[contourIndex];
      }
      contours.push_back(
          {primitive.contours[contourIndex], primitiveIndex,
           std::move(edgeOverrides), std::move(edgeVisibleOverrides),
           std::move(edgeNormalMapOverrides), std::move(edgeWallMaskOverrides),
           primitive.contributesProperties});
    }
  }

  wp::Timer timer;
  auto graph = BuildPSLG(contours);
  if (stats != nullptr) {
    stats->buildPSLGTimeNs = timer.elapsedNanoseconds();
  }
  timer.restart();

  auto cycles = ExtractMinimalCycles(graph);
  if (stats != nullptr) {
    stats->cycleExtractionTimeNs = timer.elapsedNanoseconds();
  }
  timer.restart();

  auto hierarchy = BuildPolygonHierarchy(graph, cycles);
  if (stats != nullptr) {
    stats->polygonHierarchyTimeNs = timer.elapsedNanoseconds();
  }
  timer.restart();

  auto faces = BuildFaces(hierarchy);

  auto assignCycleSide = [&](int faceIndex, int cycleIndex, bool assignLeft) {
    auto const& cycle = cycles[cycleIndex];
    for (size_t i = 0; i < cycle.eis.size(); ++i) {
      auto& edge = graph.es[cycle.eis[i]];
      auto traversalMatchesEdge = edge.vi[0] == cycle.vis[i];
      auto leftSide = traversalMatchesEdge ? 0 : 1;
      auto side = assignLeft ? leftSide : 1 - leftSide;
      edge.fi[side] = faceIndex;
    }
  };

  for (int faceIndex = 0; faceIndex < int(faces.size()); ++faceIndex) {
    auto const& face = faces[faceIndex];
    assignCycleSide(faceIndex, face.polygon, true);
    for (auto hole : face.holes) {
      assignCycleSide(faceIndex, hole, false);
    }
  }

  // A depth-first traversal needs only one primitive winding row. Crossing an
  // edge mutates its sparse primitive deltas; backtracking applies the inverse.
  // This avoids both the dense face-by-primitive matrix and full-row copies for
  // every face transition.
  struct TraversalFrame {
    int faceIndex;
    int boundaryIndex{-1};  // -1 is the outer boundary; holes follow.
    size_t edgeOffset{0};
    int entryEdge{-1};
    int entryDirection{0};
  };
  vector<int32_t> windingNumbers(primitives.size());
  vector<uint8_t> visited(faces.size());
  vector<TraversalFrame> traversal;
  traversal.reserve(faces.size());

  auto classify = [&](int faceIndex) {
    Membership membership(primitives.size());
    for (size_t primitiveIndex = 0;
         primitiveIndex < primitives.size(); ++primitiveIndex) {
      auto winding = windingNumbers[primitiveIndex];
      auto member = primitives[primitiveIndex].fillRule ==
                            bw::core::Primitive::FillRule::EvenOdd
                        ? abs(winding) % 2 == 1
                        : winding != 0;
      membership.set(primitiveIndex, member);
    }
    faces[faceIndex].membership = move(membership);
    faces[faceIndex].solid = EvaluateFold(
        primitives, faces[faceIndex].membership, foldOrder);
  };
  auto applyEdge = [&](int edgeIndex, int direction) {
    for (auto const& delta : graph.es[edgeIndex].windingDeltas) {
      windingNumbers[delta.primitiveIndex] += direction * delta.delta;
    }
  };
  auto nextEdge = [&](TraversalFrame& frame) {
    while (frame.boundaryIndex < int(faces[frame.faceIndex].holes.size())) {
      auto cycleIndex = frame.boundaryIndex < 0
                            ? faces[frame.faceIndex].polygon
                            : faces[frame.faceIndex].holes[frame.boundaryIndex];
      auto const& edges = cycles[cycleIndex].eis;
      if (frame.edgeOffset < edges.size()) {
        return edges[frame.edgeOffset++];
      }
      ++frame.boundaryIndex;
      frame.edgeOffset = 0;
    }
    return -1;
  };

  // The unbounded face is omitted while classifying, so each disconnected
  // bounded component needs one direct winding seed.
  for (int seedFace = 0; seedFace < int(faces.size()); ++seedFace) {
    if (visited[seedFace]) {
      continue;
    }

    fill(windingNumbers.begin(), windingNumbers.end(), 0);
    auto sample = SamplePoint(graph, faces[seedFace], cycles);
    for (size_t primitiveIndex = 0;
         primitiveIndex < primitives.size(); ++primitiveIndex) {
      for (auto const& contour : primitives[primitiveIndex].contours) {
        windingNumbers[primitiveIndex] += ContourWinding(sample, contour);
      }
    }

    visited[seedFace] = 1;
    classify(seedFace);
    traversal.push_back({seedFace});
    while (!traversal.empty()) {
      auto edgeIndex = nextEdge(traversal.back());
      if (edgeIndex < 0) {
        auto frame = traversal.back();
        traversal.pop_back();
        if (frame.entryEdge >= 0) {
          applyEdge(frame.entryEdge, -frame.entryDirection);
        }
        continue;
      }

      auto faceIndex = traversal.back().faceIndex;
      auto const& edge = graph.es[edgeIndex];
      if (!edge.doubleSided() ||
          (edge.fi[0] != faceIndex && edge.fi[1] != faceIndex)) {
        continue;
      }
      auto neighbour = edge.fi[0] == faceIndex ? edge.fi[1] : edge.fi[0];
      if (neighbour == faceIndex || visited[neighbour]) {
        continue;
      }

      auto direction = edge.fi[1] == faceIndex ? 1 : -1;
      applyEdge(edgeIndex, direction);
      visited[neighbour] = 1;
      classify(neighbour);
      traversal.push_back({neighbour, -1, 0, edgeIndex, direction});
    }
  }

  auto result = make_shared<ArrangementResult>();
  result->vertices = graph.vs;
  result->palette.emplace_back();  // Exterior and empty faces.
  result->chipParametersPalette.emplace_back();
  for (auto const& primitive : primitives) {
    result->palette.push_back(primitive.properties);
    result->chipParametersPalette.push_back(primitive.chipParameters);
    result->primitiveOperations.push_back(primitive.operation);
    result->primitiveRawAreas.push_back(primitive.rawArea);
    result->audioEmitters.insert(
        result->audioEmitters.end(), primitive.audioEmitters.begin(),
        primitive.audioEmitters.end());
  }

  // Face zero is the unbounded exterior, allowing every edge to name two
  // valid incident faces. Its empty outer boundary distinguishes it from all
  // bounded arrangement faces; each root cycle is one of its explicit inner
  // boundaries.
  ArrangementFace exteriorFace;
  exteriorFace.membership = Membership(primitives.size());
  exteriorFace.solidContributors = Membership(primitives.size());
  for (auto const& node : hierarchy) {
    if (node.parent >= 0) {
      continue;
    }
    vector<uint32_t> boundary;
    vector<uint32_t> boundaryVertices;
    auto const& cycle = cycles[node.cycleIndex];
    for (auto edgeIndex : cycle.eis) {
      boundary.push_back(uint32_t(edgeIndex));
    }
    for (auto vertexIndex : cycle.vis) {
      boundaryVertices.push_back(uint32_t(vertexIndex));
    }
    exteriorFace.innerBoundaries.push_back(move(boundary));
    exteriorFace.innerBoundaryVertices.push_back(move(boundaryVertices));
  }
  result->faces.push_back(move(exteriorFace));

  for (auto& face : faces) {
    ArrangementFace outputFace;
    auto const& outerCycle = cycles[face.polygon];
    for (auto edgeIndex : outerCycle.eis) {
      outputFace.outerBoundary.push_back(uint32_t(edgeIndex));
    }
    for (auto vertexIndex : outerCycle.vis) {
      outputFace.outerBoundaryVertices.push_back(uint32_t(vertexIndex));
    }
    for (auto hole : face.holes) {
      vector<uint32_t> boundary;
      vector<uint32_t> boundaryVertices;
      for (auto edgeIndex : cycles[hole].eis) {
        boundary.push_back(uint32_t(edgeIndex));
      }
      for (auto vertexIndex : cycles[hole].vis) {
        boundaryVertices.push_back(uint32_t(vertexIndex));
      }
      outputFace.innerBoundaries.push_back(move(boundary));
      outputFace.innerBoundaryVertices.push_back(move(boundaryVertices));
    }
    outputFace.solid = face.solid;
    outputFace.solidContributors = Membership(primitives.size());
    bool contributorSolid = false;
    for (auto primitiveIndex : foldOrder) {
      auto const member = face.membership.contains(primitiveIndex);
      switch (primitives[primitiveIndex].operation) {
        case Primitive::Operation::Union:
          if (member) {
            contributorSolid = true;
            outputFace.solidContributors.set(primitiveIndex);
          }
          break;
        case Primitive::Operation::Intersection:
          if (!member) {
            contributorSolid = false;
            outputFace.solidContributors = Membership(primitives.size());
          } else if (contributorSolid) {
            outputFace.solidContributors.set(primitiveIndex);
          }
          break;
        case Primitive::Operation::Difference:
          if (member) {
            contributorSolid = false;
            outputFace.solidContributors = Membership(primitives.size());
          }
          break;
        case Primitive::Operation::XOR:
          if (member) {
            if (contributorSolid) {
              contributorSolid = false;
              outputFace.solidContributors = Membership(primitives.size());
            } else {
              contributorSolid = true;
              outputFace.solidContributors.set(primitiveIndex);
            }
          }
          break;
      }
    }

    // The old engine associates properties with the Union that starts an
    // intermediate fold run. Later operations modify that run without taking
    // ownership. The highest-priority solid run wins where runs overlap, with
    // the first run winning an equal-priority tie.
    int winningPrimitive = -1;
    int runPrimitive = -1;
    bool runSolid = false;
    auto finishRun = [&] {
      // Structural Primitives still shape the run, but property ownership is
      // selected only from authored, property-contributing run starters.
      if (runSolid && primitives[runPrimitive].contributesProperties &&
          (winningPrimitive < 0 ||
           primitives[runPrimitive].priority >
               primitives[winningPrimitive].priority)) {
        winningPrimitive = runPrimitive;
      }
    };
    for (auto primitiveIndex : foldOrder) {
      auto member = face.membership.contains(primitiveIndex);
      auto operation = primitives[primitiveIndex].operation;
      if (runPrimitive < 0 ||
          operation == bw::core::Primitive::Operation::Union) {
        finishRun();
        runPrimitive = int(primitiveIndex);
        runSolid = member;
        continue;
      }
      runSolid = ApplyOperation(operation, runSolid, member);
    }
    finishRun();
    // Empty faces retain their highest-priority member for inspection. A
    // solid face also needs that fallback when the run fold has no winner.
    if (!face.solid || winningPrimitive < 0) {
      winningPrimitive = -1;
      for (auto primitiveIndex : foldOrder) {
        if (face.membership.contains(primitiveIndex) &&
            (!face.solid ||
             primitives[primitiveIndex].contributesProperties) &&
            (winningPrimitive < 0 ||
             primitives[primitiveIndex].priority >
                 primitives[winningPrimitive].priority)) {
          winningPrimitive = int(primitiveIndex);
        }
      }
    }
    if (winningPrimitive >= 0) {
      outputFace.paletteIndex = uint16_t(winningPrimitive + 1);
      outputFace.primitiveIndex =
          primitives[winningPrimitive].primitiveIndex;
      outputFace.operation = primitives[winningPrimitive].operation;
      outputFace.contributesProperties =
          primitives[winningPrimitive].contributesProperties;
    }
    outputFace.membership = move(face.membership);
    result->faces.push_back(move(outputFace));
  }

  for (auto const& edge : graph.es) {
    result->edges.push_back({{uint32_t(edge.vi[0]), uint32_t(edge.vi[1])},
                             {edge.fi[0] < 0 ? 0u : uint32_t(edge.fi[0] + 1),
                              edge.fi[1] < 0 ? 0u : uint32_t(edge.fi[1] + 1)},
                             edge.collidesOverride,
                             edge.visibleOverride,
                             edge.normalMapOverride,
                             edge.wallMaskOverride});
  }

  // A floor-minus-ceiling difference is affine, so its maximum over a
  // polygonal face occurs at one of the outer or hole boundary vertices.
  // Validate the generated regions, after property ownership has been folded,
  // without introducing elevation-only points into Arrangement topology.
  for (uint32_t faceIndex = 1; faceIndex < result->faces.size(); ++faceIndex) {
    auto const& face = result->faces[faceIndex];
    if (!face.solid) {
      continue;
    }
    auto const& properties = result->palette[face.paletteIndex];
    auto validateBoundary = [&](vector<uint32_t> const& vertices) {
      for (auto vertexIndex : vertices) {
        auto const& fixed = result->vertices[vertexIndex];
        wp::Vector2 const position{
            ToWorldCoordinate(fixed.x), ToWorldCoordinate(fixed.y)};
        if (properties.floorZ.evaluate(position) >
            properties.ceilingZ.evaluate(position)) {
          throw CoreException(format(
              "Generation rejected solid region {}: floor elevation exceeds ceiling elevation at ({}, {})",
              faceIndex, position.x, position.y));
        }
      }
    };
    validateBoundary(face.outerBoundaryVertices);
    for (auto const& boundary : face.innerBoundaryVertices) {
      validateBoundary(boundary);
    }
  }

  if (stats != nullptr) {
    stats->vertexCount = uint32_t(result->vertices.size());
    stats->edgeCount = uint32_t(result->edges.size());
    stats->faceCount = uint32_t(result->faces.size());
    stats->classificationTimeNs = timer.elapsedNanoseconds();
  }
  return result;
}

static int PointInBoundary(
    FixedPointVertex const& point,
    vector<uint32_t> const& boundary,
    ArrangementResult const& arrangement) {
  int crossings = 0;
  for (auto edgeIndex : boundary) {
    auto const& edge = arrangement.edges[edgeIndex];
    auto const& a = arrangement.vertices[edge.v[0]];
    auto const& b = arrangement.vertices[edge.v[1]];
    if (PointOnSegment(point, a, b)) {
      return -1;
    }
    if ((a.y > point.y) != (b.y > point.y)) {
      auto cross = Cross(a, b, point);
      if ((b.y > a.y && cross > 0) ||
          (b.y < a.y && cross < 0)) {
        ++crossings;
      }
    }
  }
  return crossings % 2;
}

bool PointInFace(
    FixedPointVertex const& point,
    ArrangementFace const& face,
    ArrangementResult const& arrangement) {
  if (face.outerBoundary.empty() ||
      PointInBoundary(point, face.outerBoundary, arrangement) <= 0) {
    return false;
  }
  for (auto const& hole : face.innerBoundaries) {
    if (PointInBoundary(point, hole, arrangement) > 0) {
      return false;
    }
  }
  return true;
}

static int64_t BoundaryArea2(
    vector<uint32_t> const& boundaryVertices,
    ArrangementResult const& arrangement) {
  int64_t area = 0;
  for (size_t i = 0; i < boundaryVertices.size(); ++i) {
    auto const& a = arrangement.vertices[boundaryVertices[i]];
    auto const& b =
        arrangement.vertices[boundaryVertices[(i + 1) % boundaryVertices.size()]];
    area += a.x * b.y - a.y * b.x;
  }
  return area;
}

double FaceArea(ArrangementFace const& face, ArrangementResult const& arrangement) {
  if (face.outerBoundaryVertices.empty()) {
    return 0.0;
  }
  auto area2 = BoundaryArea2(face.outerBoundaryVertices, arrangement);
  for (auto const& hole : face.innerBoundaryVertices) {
    area2 -= BoundaryArea2(hole, arrangement);
  }
  return ToWorldArea(area2);
}

vector<ArrangementTriangle> BuildArrangementTriangles(
    ArrangementResult const& arrangement) {
  using EarcutPoint = array<double, 2>;
  vector<ArrangementTriangle> triangles;

  for (uint32_t faceIndex = 0;
       faceIndex < uint32_t(arrangement.faces.size()); ++faceIndex) {
    auto const& face = arrangement.faces[faceIndex];
    if (!face.solid || face.outerBoundary.empty()) {
      continue;
    }

    vector<vector<EarcutPoint>> polygons;
    vector<uint32_t> vertexIndices;
    auto addBoundary = [&](vector<uint32_t> const& boundaryVertices) {
      vector<EarcutPoint> polygon;
      for (auto vertexIndex : boundaryVertices) {
        auto const& vertex = arrangement.vertices[vertexIndex];
        polygon.push_back(
            {double(vertex.x) / FixedPointUnitsPerWorldUnit,
             double(vertex.y) / FixedPointUnitsPerWorldUnit});
        vertexIndices.push_back(vertexIndex);
      }
      polygons.push_back(move(polygon));
    };

    addBoundary(face.outerBoundaryVertices);
    for (auto const& hole : face.innerBoundaryVertices) {
      addBoundary(hole);
    }

    auto const& properties = arrangement.palette[face.paletteIndex];
    auto floorNormal = properties.floorZ.normal();
    auto ceilingUp = properties.ceilingZ.normal();
    array<float, 3> ceilingNormal{
        -ceilingUp[0], -ceilingUp[1], -ceilingUp[2]};

    auto indices = mapbox::earcut<uint32_t>(polygons);
    for (size_t i = 0; i < indices.size(); i += 3) {
      ArrangementTriangle triangle{
          {vertexIndices[indices[i]],
           vertexIndices[indices[i + 1]],
           vertexIndices[indices[i + 2]]},
          faceIndex};
      triangle.floor.normal = floorNormal;
      triangle.ceiling.normal = ceilingNormal;
      for (size_t corner = 0; corner < 3; ++corner) {
        auto const& fixed = arrangement.vertices[triangle.v[corner]];
        wp::Vector2 position{
            ToWorldCoordinate(fixed.x), ToWorldCoordinate(fixed.y)};
        triangle.floor.elevation[corner] =
            properties.floorZ.evaluate(position);
        triangle.ceiling.elevation[corner] =
            properties.ceilingZ.evaluate(position);
      }
      triangles.push_back(std::move(triangle));
    }
  }
  return triangles;
}

namespace {
double TriangleArea(std::array<wp::Vector2, 3> const& positions) {
  auto const ab = positions[1] - positions[0];
  auto const ac = positions[2] - positions[0];
  return 0.5 * std::abs(
                   double(ab.x) * double(ac.y) -
                   double(ab.y) * double(ac.x));
}

// Integral of max(elevation - plane, 0) over a triangle whose affine plane is
// represented by its three corner elevations. Linear clipping leaves either
// the whole triangle, one similar corner triangle, or the whole signed prism
// less one similar negative corner triangle.
double PositivePartVolume(
    double area,
    std::array<float, 3> const& plane,
    double elevation) {
  std::array<double, 3> depth{
      elevation - plane[0], elevation - plane[1], elevation - plane[2]};
  auto positiveCount = std::ranges::count_if(
      depth, [](double value) { return value > 0.0; });
  if (positiveCount == 0) return 0.0;
  if (positiveCount == 3) {
    return area * (depth[0] + depth[1] + depth[2]) / 3.0;
  }

  if (positiveCount == 1) {
    auto positive = size_t(std::ranges::find_if(
                               depth, [](double value) { return value > 0.0; }) -
                           depth.begin());
    auto other0 = (positive + 1) % 3;
    auto other1 = (positive + 2) % 3;
    auto d = depth[positive];
    auto ratio0 = d / (d - depth[other0]);
    auto ratio1 = d / (d - depth[other1]);
    return area * ratio0 * ratio1 * d / 3.0;
  }

  auto negative = size_t(std::ranges::find_if(
                             depth, [](double value) { return value <= 0.0; }) -
                         depth.begin());
  auto other0 = (negative + 1) % 3;
  auto other1 = (negative + 2) % 3;
  auto magnitude = -depth[negative];
  auto ratio0 = magnitude / (depth[other0] + magnitude);
  auto ratio1 = magnitude / (depth[other1] + magnitude);
  auto signedVolume = area * (depth[0] + depth[1] + depth[2]) / 3.0;
  return signedVolume + area * ratio0 * ratio1 * magnitude / 3.0;
}
}  // namespace

double HydraulicCell::volumeBelow(double elevation) const {
  std::array<float, 3> floorElevations;
  std::array<float, 3> ceilingElevations;
  for (size_t corner = 0; corner < positions.size(); ++corner) {
    floorElevations[corner] = floor.evaluate(positions[corner]);
    ceilingElevations[corner] = ceiling.evaluate(positions[corner]);
  }

  auto minimumFloor =
      double(*std::min_element(floorElevations.begin(), floorElevations.end()));
  if (elevation <= minimumFloor) return 0.0;

  auto maximumCeiling = double(
      *std::max_element(ceilingElevations.begin(), ceilingElevations.end()));
  if (elevation >= maximumCeiling) {
    auto clearanceSum = 0.0;
    for (size_t corner = 0; corner < 3; ++corner) {
      clearanceSum += ceilingElevations[corner] - floorElevations[corner];
    }
    return worldArea * std::max(0.0, clearanceSum) / 3.0;
  }

  // clamp(elevation - floor, 0, ceiling - floor) equals the positive
  // part above the floor minus the positive part above the ceiling.
  return std::max(
      0.0, PositivePartVolume(worldArea, floorElevations, elevation) -
               PositivePartVolume(worldArea, ceilingElevations, elevation));
}

vector<HydraulicCell> BuildHydraulicCells(
    ArrangementResult const& arrangement,
    vector<ArrangementTriangle> const& triangles) {
  vector<HydraulicCell> cells;
  cells.reserve(triangles.size());
  for (auto const& triangle : triangles) {
    HydraulicCell cell;
    cell.triangle = triangle;
    auto const& properties =
        arrangement.palette[arrangement.faces[triangle.face].paletteIndex];
    cell.floor = properties.floorZ;
    cell.ceiling = properties.ceilingZ;
    for (size_t corner = 0; corner < cell.positions.size(); ++corner) {
      auto const& fixed = arrangement.vertices[triangle.v[corner]];
      cell.positions[corner] = {
          ToWorldCoordinate(fixed.x), ToWorldCoordinate(fixed.y)};
    }
    cell.worldArea = std::abs(ToWorldArea(Cross(
        arrangement.vertices[triangle.v[0]],
        arrangement.vertices[triangle.v[1]],
        arrangement.vertices[triangle.v[2]])));
    cells.push_back(std::move(cell));
  }
  return cells;
}

vector<ArrangementWall> BuildArrangementWalls(
    ArrangementResult const& arrangement) {
  vector<ArrangementWall> walls;
  for (uint32_t edgeIndex = 0;
       edgeIndex < uint32_t(arrangement.edges.size()); ++edgeIndex) {
    auto const& edge = arrangement.edges[edgeIndex];
    auto const& face0 = arrangement.faces[edge.face[0]];
    auto const& face1 = arrangement.faces[edge.face[1]];
    array<wp::Vector2, 2> endpointPositions;
    for (size_t endpoint = 0; endpoint < endpointPositions.size(); ++endpoint) {
      auto const& fixed = arrangement.vertices[edge.v[endpoint]];
      endpointPositions[endpoint] = {
          ToWorldCoordinate(fixed.x), ToWorldCoordinate(fixed.y)};
    }
    auto evaluate = [&](Elevation const& elevation) {
      return array<float, 2>{
          elevation.evaluate(endpointPositions[0]),
          elevation.evaluate(endpointPositions[1])};
    };
    auto sample = [](array<float, 2> const& values, float parameter) {
      return lerp(values[0], values[1], parameter);
    };
    auto appendWall = [&](uint16_t paletteIndex, ArrangementWallKind kind,
                          array<float, 2> const& bottomZ,
                          array<float, 2> const& topZ,
                          array<float, 2> const& sourceEdgeParameter,
                          float clearance, uint32_t frontFace,
                          uint32_t ownerFace) {
      walls.push_back(
          {edgeIndex,
           *min_element(bottomZ.begin(), bottomZ.end()),
           *max_element(topZ.begin(), topZ.end()),
           paletteIndex,
           kind,
           clearance,
           edge.visibleOverride.value_or(true),
           edge.normalMapOverride.value_or(WallNormalMapOverride::unset()),
           edge.wallMaskOverride.value_or(WallMaskOverride::unset()),
           bottomZ,
           topZ,
           sourceEdgeParameter,
           frontFace,
           ownerFace});
    };

    if (face0.solid != face1.solid) {
      auto solidFaceIndex = face0.solid ? edge.face[0] : edge.face[1];
      auto emptyFaceIndex = face0.solid ? edge.face[1] : edge.face[0];
      auto const& solidFace = arrangement.faces[solidFaceIndex];
      auto const& emptyFace = arrangement.faces[emptyFaceIndex];
      auto const& properties = arrangement.palette[solidFace.paletteIndex];
      // An authored Difference contributes no solid Face of its own, but its
      // boundary creates the newly exposed wall. Property-transparent
      // structural Differences instead leave ownership with the surviving
      // solid Face. In either case that Face supplies the vertical span.
      auto ownerFace =
          emptyFace.operation == bw::core::Primitive::Operation::Difference &&
                  emptyFace.contributesProperties
              ? emptyFaceIndex
              : solidFaceIndex;
      auto bottomZ = evaluate(properties.floorZ);
      auto topZ = evaluate(properties.ceilingZ);
      appendWall(
          arrangement.faces[ownerFace].paletteIndex,
          ArrangementWallKind::Border, bottomZ, topZ, {0.0f, 1.0f},
          min(topZ[0] - bottomZ[0], topZ[1] - bottomZ[1]), solidFaceIndex,
          ownerFace);
      continue;
    }
    if (!face0.solid) {
      continue;
    }

    auto const& properties0 = arrangement.palette[face0.paletteIndex];
    auto const& properties1 = arrangement.palette[face1.paletteIndex];
    auto floor0 = evaluate(properties0.floorZ);
    auto floor1 = evaluate(properties1.floorZ);
    auto ceiling0 = evaluate(properties0.ceilingZ);
    auto ceiling1 = evaluate(properties1.ceilingZ);
    auto clearanceAt = [&](float parameter) {
      return min(sample(ceiling0, parameter), sample(ceiling1, parameter)) -
             max(sample(floor0, parameter), sample(floor1, parameter));
    };

    // Along one source edge the difference between two affine Elevation
    // planes is linear. Split at its one possible interior zero so each
    // derived segment has one unambiguous front side and material owner. The
    // crossing remains floating-point derived output, never Arrangement
    // topology.
    auto appendStepSegments = [&](ArrangementWallKind kind,
                                  array<float, 2> const& side0,
                                  array<float, 2> const& side1) {
      array<float, 2> difference{
          side0[0] - side1[0], side0[1] - side1[1]};
      array<float, 3> cuts{0.0f, 1.0f, 1.0f};
      size_t cutCount = 2;
      optional<float> crossing;
      if ((difference[0] < 0.0f && difference[1] > 0.0f) ||
          (difference[0] > 0.0f && difference[1] < 0.0f)) {
        crossing = difference[0] / (difference[0] - difference[1]);
        cuts[1] = *crossing;
        cuts[2] = 1.0f;
        cutCount = 3;
      }

      for (size_t segment = 0; segment + 1 < cutCount; ++segment) {
        auto start = cuts[segment];
        auto end = cuts[segment + 1];
        if (!(end > start)) continue;
        auto midpoint = (start + end) * 0.5f;
        auto midpointDifference =
            sample(side0, midpoint) - sample(side1, midpoint);
        if (midpointDifference == 0.0f) continue;

        auto sideAt = [&](array<float, 2> const& values, float parameter) {
          return sample(values, parameter);
        };
        array<float, 2> segmentSide0{
            sideAt(side0, start), sideAt(side0, end)};
        array<float, 2> segmentSide1{
            sideAt(side1, start), sideAt(side1, end)};
        // Both formulae describe exactly the same crossing. Pin the two
        // independently rounded samples to one value so a triangular wall has
        // one shared tip rather than a microscopic fourth edge.
        for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
          auto parameter = endpoint == 0 ? start : end;
          if (crossing && parameter == *crossing) {
            auto common =
                (segmentSide0[endpoint] + segmentSide1[endpoint]) * 0.5f;
            segmentSide0[endpoint] = common;
            segmentSide1[endpoint] = common;
          }
        }

        array<float, 2> bottomZ{
            min(segmentSide0[0], segmentSide1[0]),
            min(segmentSide0[1], segmentSide1[1])};
        array<float, 2> topZ{
            max(segmentSide0[0], segmentSide1[0]),
            max(segmentSide0[1], segmentSide1[1])};
        auto face0Owns = kind == ArrangementWallKind::FloorStep
                             ? midpointDifference > 0.0f
                             : midpointDifference < 0.0f;
        auto ownerFace = face0Owns ? edge.face[0] : edge.face[1];
        auto frontFace = face0Owns ? edge.face[1] : edge.face[0];
        appendWall(
            arrangement.faces[ownerFace].paletteIndex, kind, bottomZ, topZ,
            {start, end}, min(clearanceAt(start), clearanceAt(end)),
            frontFace, ownerFace);
      }
    };

    appendStepSegments(ArrangementWallKind::FloorStep, floor0, floor1);
    appendStepSegments(
        ArrangementWallKind::CeilingStep, ceiling0, ceiling1);
  }
  return walls;
}

vector<LiquidAdjacency> BuildLiquidAdjacency(ArrangementResult const& arrangement) {
  // Derive this legacy face-level topology view from the authoritative
  // Hydraulic graph. In particular, do not compare Elevation planes through
  // their base elevations: a sloped or pinched edge is adjacent exactly when
  // BuildHydraulicLinks finds a positive-clearance traversable portion.
  auto triangles = BuildArrangementTriangles(arrangement);
  auto cells = BuildHydraulicCells(arrangement, triangles);
  auto links = BuildHydraulicLinks(arrangement, cells);

  vector<LiquidAdjacency> result;
  for (auto const& link : links) {
    auto face0 = cells[link.cell0].triangle.face;
    auto face1 = link.drain ? uint32_t{0}
                            : cells[link.cell1].triangle.face;
    if (face0 == face1) continue;
    result.push_back(
        {min(face0, face1), max(face0, face1), link.drain});
  }

  sort(result.begin(), result.end(), [](auto const& a, auto const& b) {
    return tie(a.face0, a.face1) < tie(b.face0, b.face1);
  });
  result.erase(
      unique(
          result.begin(), result.end(),
          [](auto const& a, auto const& b) {
            return a.face0 == b.face0 && a.face1 == b.face1;
          }),
      result.end());
  return result;
}

namespace {
using HydraulicEdgeKey = pair<uint32_t, uint32_t>;

HydraulicEdgeKey HydraulicEdge(uint32_t vertex0, uint32_t vertex1) {
  return {min(vertex0, vertex1), max(vertex0, vertex1)};
}

optional<double> FindHydraulicSill(
    HydraulicCell const& cell0,
    HydraulicCell const& cell1,
    FixedPointVertex const& fixed0,
    FixedPointVertex const& fixed1) {
  wp::Vector2 position0{
      ToWorldCoordinate(fixed0.x), ToWorldCoordinate(fixed0.y)};
  wp::Vector2 position1{
      ToWorldCoordinate(fixed1.x), ToWorldCoordinate(fixed1.y)};
  array<array<double, 2>, 4> functions{{
      {cell0.floor.evaluate(position0), cell0.floor.evaluate(position1)},
      {cell1.floor.evaluate(position0), cell1.floor.evaluate(position1)},
      {cell0.ceiling.evaluate(position0), cell0.ceiling.evaluate(position1)},
      {cell1.ceiling.evaluate(position0), cell1.ceiling.evaluate(position1)},
  }};

  // Between crossings, the maximum floor and minimum ceiling are each one
  // affine function and their ordering cannot change. Include every pairwise
  // crossing so this remains true at floor/floor, ceiling/ceiling, and all
  // floor/ceiling changes without relying on which function currently wins.
  vector<double> cuts{0.0, 1.0};
  for (size_t first = 0; first < functions.size(); ++first) {
    for (size_t second = first + 1; second < functions.size(); ++second) {
      auto difference0 = functions[first][0] - functions[second][0];
      auto difference1 = functions[first][1] - functions[second][1];
      auto denominator = difference0 - difference1;
      if (denominator == 0.0) continue;
      auto crossing = difference0 / denominator;
      if (crossing > 0.0 && crossing < 1.0) cuts.push_back(crossing);
    }
  }
  sort(cuts.begin(), cuts.end());
  cuts.erase(unique(cuts.begin(), cuts.end()), cuts.end());

  auto sample = [&](size_t function, double parameter) {
    return lerp(
        functions[function][0], functions[function][1], parameter);
  };
  auto opening = [&](double parameter) {
    auto floor = max(sample(0, parameter), sample(1, parameter));
    auto ceiling = min(sample(2, parameter), sample(3, parameter));
    return floor < ceiling;
  };
  auto bottom = [&](double parameter) {
    return max(sample(0, parameter), sample(1, parameter));
  };

  auto sill = numeric_limits<double>::infinity();
  for (size_t interval = 0; interval + 1 < cuts.size(); ++interval) {
    auto begin = cuts[interval];
    auto end = cuts[interval + 1];
    if (!(end > begin) || !opening(midpoint(begin, end))) continue;

    // The opening itself is strict, but its hydraulic threshold is the
    // infimum at its closure too: once a surface reaches that elevation an
    // arbitrarily nearby positive-width part of the edge is traversable.
    sill = min(sill, min(bottom(begin), bottom(end)));
  }
  if (!isfinite(sill)) return nullopt;
  return sill;
}
}  // namespace

vector<HydraulicLink> BuildHydraulicLinks(
    ArrangementResult const& arrangement,
    vector<HydraulicCell> const& cells) {
  map<HydraulicEdgeKey, vector<uint32_t>> occurrences;
  for (uint32_t cellIndex = 0; cellIndex < uint32_t(cells.size());
       ++cellIndex) {
    auto const& triangle = cells[cellIndex].triangle;
    for (size_t edge = 0; edge < 3; ++edge) {
      occurrences[HydraulicEdge(
                      triangle.v[edge], triangle.v[(edge + 1) % 3])]
          .push_back(cellIndex);
    }
  }

  map<HydraulicEdgeKey, uint32_t> arrangementEdges;
  for (uint32_t edgeIndex = 0;
       edgeIndex < uint32_t(arrangement.edges.size()); ++edgeIndex) {
    auto const& edge = arrangement.edges[edgeIndex];
    arrangementEdges[HydraulicEdge(edge.v[0], edge.v[1])] = edgeIndex;
  }

  using LinkKey = tuple<uint32_t, uint32_t, bool>;
  map<LinkKey, double> lowestSills;
  auto append = [&](uint32_t cell0, uint32_t cell1, bool drain,
                    HydraulicEdgeKey const& edgeKey) {
    auto const& cell = cells[cell0];
    auto const& neighbour = drain ? cell : cells[cell1];
    auto sill = FindHydraulicSill(
        cell, neighbour, arrangement.vertices[edgeKey.first],
        arrangement.vertices[edgeKey.second]);
    if (!sill) return;

    if (!drain && cell1 < cell0) swap(cell0, cell1);
    LinkKey key{cell0, drain ? HydraulicDrainCell : cell1, drain};
    auto [entry, inserted] = lowestSills.emplace(key, *sill);
    if (!inserted) entry->second = min(entry->second, *sill);
  };

  for (auto const& [edgeKey, edgeCells] : occurrences) {
    if (edgeCells.size() == 2) {
      auto cell0 = edgeCells[0];
      auto cell1 = edgeCells[1];
      auto face0 = cells[cell0].triangle.face;
      auto face1 = cells[cell1].triangle.face;
      if (face0 != face1) {
        auto source = arrangementEdges.find(edgeKey);
        if (source == arrangementEdges.end()) continue;
        auto const& edge = arrangement.edges[source->second];
        auto incident =
            (edge.face[0] == face0 && edge.face[1] == face1) ||
            (edge.face[0] == face1 && edge.face[1] == face0);
        if (!incident) continue;
      }
      // Same-face pairs are artificial triangulation edges (including Earcut
      // bridges around holes). They are open geometrically, but their affine
      // floor still gives them a real Sill.
      append(cell0, cell1, false, edgeKey);
      continue;
    }

    if (edgeCells.size() != 1) continue;
    auto source = arrangementEdges.find(edgeKey);
    if (source == arrangementEdges.end()) continue;
    auto const& edge = arrangement.edges[source->second];
    if (edge.face[0] != 0 && edge.face[1] != 0) continue;
    auto cell = edgeCells.front();
    auto roomFace = edge.face[0] == 0 ? edge.face[1] : edge.face[0];
    if (cells[cell].triangle.face != roomFace ||
        !arrangement.faces[roomFace].solid ||
        edge.collidesOverride.value_or(true)) {
      continue;
    }
    append(cell, HydraulicDrainCell, true, edgeKey);
  }

  vector<HydraulicLink> result;
  result.reserve(lowestSills.size());
  for (auto const& [key, sill] : lowestSills) {
    auto [cell0, cell1, drain] = key;
    result.push_back({cell0, cell1, sill, drain});
  }
  sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
    return tie(left.sill, left.cell0, left.cell1, left.drain) <
           tie(right.sill, right.cell0, right.cell1, right.drain);
  });
  return result;
}

static vector<double> ComputeUndistributedLiquidDepthsDouble(
    ArrangementResult const& arrangement) {
  auto faceCount = uint32_t(arrangement.faces.size());
  vector<double> depths(faceCount, 0.0);
  auto primitiveCount = arrangement.primitiveOperations.size();

  // Shared by both passes below, since a face's area is needed once to total
  // up each Primitive's surviving footprint and again to seed that face.
  vector<double> faceAreas(faceCount, 0.0);
  for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    if (!arrangement.faces[faceIndex].solid) {
      continue;
    }
    faceAreas[faceIndex] =
        max(0.0, FaceArea(arrangement.faces[faceIndex], arrangement));
  }

  // How much of each Union Primitive's footprint survived the fold. The fold
  // routinely splits one Primitive across several faces without removing any
  // of it - any other Primitive's edge crossing it does that - so this total,
  // not the area of whichever single face is being seeded, is what the
  // authored volume has to spread over. Depth therefore cannot be decided
  // from one face alone, which is why this is a separate pass.
  vector<double> survivingAreas(primitiveCount, 0.0);
  for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    if (faceAreas[faceIndex] <= 0.0) {
      continue;
    }
    auto const& face = arrangement.faces[faceIndex];
    for (size_t primitiveIndex = 0; primitiveIndex < primitiveCount;
         ++primitiveIndex) {
      if (arrangement.primitiveOperations[primitiveIndex] ==
              bw::core::Primitive::Operation::Union &&
          face.membership.contains(primitiveIndex)) {
        survivingAreas[primitiveIndex] += faceAreas[faceIndex];
      }
    }
  }

  for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    if (faceAreas[faceIndex] <= 0.0) {
      continue;
    }
    auto const& face = arrangement.faces[faceIndex];

    double depth = 0.0;
    for (size_t primitiveIndex = 0; primitiveIndex < primitiveCount;
         ++primitiveIndex) {
      if (arrangement.primitiveOperations[primitiveIndex] !=
              bw::core::Primitive::Operation::Union ||
          !face.membership.contains(primitiveIndex)) {
        continue;
      }
      auto survivingArea = survivingAreas[primitiveIndex];
      if (survivingArea <= 0.0) {
        continue;
      }
      // liquidLevel * rawArea is the authored volume; spreading it at one
      // uniform depth over the surviving footprint conserves it exactly,
      // whether the fold merely subdivided that footprint (every piece seeds
      // at the same depth) or carved part of it away (what is left stands
      // correspondingly deeper).
      auto liquidLevel = arrangement.palette[primitiveIndex + 1].liquidLevel;
      depth += double(liquidLevel) *
               arrangement.primitiveRawAreas[primitiveIndex] / survivingArea;
    }
    if (depth <= 0.0) {
      continue;
    }

    // Deliberately unclamped: capping here would destroy volume that the
    // equilibrium pass needs to spread into neighbouring faces.
    depths[faceIndex] = depth;
  }
  return depths;
}

vector<float> ComputeUndistributedLiquidDepths(
    ArrangementResult const& arrangement) {
  auto precise = ComputeUndistributedLiquidDepthsDouble(arrangement);
  vector<float> depths;
  depths.reserve(precise.size());
  ranges::transform(
      precise, back_inserter(depths),
      [](double depth) { return static_cast<float>(depth); });
  return depths;
}

namespace {
// Integrated capacity of one prospective Pool at a horizontal elevation.
double LiquidCapacityBelow(
    vector<HydraulicCell> const& cells,
    vector<uint32_t> const& cellIndices,
    double elevation) {
  double total = 0.0;
  for (auto cellIndex : cellIndices) {
    total += cells[cellIndex].volumeBelow(elevation);
  }
  return total;
}

std::array<double, 2> LiquidElevationBounds(
    vector<HydraulicCell> const& cells,
    vector<uint32_t> const& cellIndices) {
  std::array<double, 2> bounds{
      numeric_limits<double>::infinity(),
      -numeric_limits<double>::infinity()};
  for (auto cellIndex : cellIndices) {
    auto const& cell = cells[cellIndex];
    for (auto const& position : cell.positions) {
      bounds[0] = min(bounds[0], double(cell.floor.evaluate(position)));
      bounds[1] = max(bounds[1], double(cell.ceiling.evaluate(position)));
    }
  }
  return bounds;
}

// Sloped-cell capacity is piecewise cubic. Fixed-count double-precision
// bisection is deterministic for identical generated input and makes no
// assumption that capacity is linear in elevation.
double SolveLiquidLevel(
    vector<HydraulicCell> const& cells,
    vector<uint32_t> const& cellIndices,
    double volume) {
  if (cellIndices.empty()) {
    return -numeric_limits<double>::infinity();
  }
  auto bounds = LiquidElevationBounds(cells, cellIndices);
  if (volume <= 0.0) return bounds[0];
  if (LiquidCapacityBelow(cells, cellIndices, bounds[1]) <= volume) {
    return bounds[1];
  }

  auto lower = bounds[0];
  auto upper = bounds[1];
  // 64 iterations reduce even the full float elevation range below one double
  // ULP for ordinary World scales while keeping the work data-independent.
  for (int iteration = 0; iteration < 64; ++iteration) {
    auto middle = std::midpoint(lower, upper);
    if (LiquidCapacityBelow(cells, cellIndices, middle) < volume) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  // Return the capacity-satisfying side of the bracket so a Pool whose exact
  // equilibrium is a Sill still reaches it rather than rounding just below.
  return upper;
}

struct ClipVertex {
  wp::Vector2 position;
  double floorDistance;
  double ceilingDistance;
};

vector<ClipVertex> ClipLiquidPolygon(
    vector<ClipVertex> polygon,
    bool floorBoundary) {
  vector<ClipVertex> result;
  if (polygon.empty()) return result;
  auto distance = [&](ClipVertex const& vertex) {
    return floorBoundary ? vertex.floorDistance : vertex.ceilingDistance;
  };
  auto previous = polygon.back();
  auto previousInside = distance(previous) >= 0.0;
  for (auto const& current : polygon) {
    auto currentInside = distance(current) >= 0.0;
    if (currentInside != previousInside) {
      auto previousDistance = distance(previous);
      auto currentDistance = distance(current);
      auto parameter = previousDistance /
                       (previousDistance - currentDistance);
      result.push_back(
          {previous.position +
               (current.position - previous.position) * float(parameter),
           std::lerp(previous.floorDistance, current.floorDistance, parameter),
           std::lerp(
               previous.ceilingDistance, current.ceilingDistance,
               parameter)});
    }
    if (currentInside) result.push_back(current);
    previous = current;
    previousInside = currentInside;
  }
  return result;
}

vector<LiquidSurfaceTriangle> BuildLiquidSurfaceTriangles(
    vector<HydraulicCell> const& cells,
    vector<double> const& poolElevations) {
  vector<LiquidSurfaceTriangle> result;
  for (uint32_t cellIndex = 0; cellIndex < uint32_t(cells.size());
       ++cellIndex) {
    auto elevation = poolElevations[cellIndex];
    if (!std::isfinite(elevation) ||
        cells[cellIndex].volumeBelow(elevation) <= 0.0) {
      continue;
    }
    auto const& cell = cells[cellIndex];
    vector<ClipVertex> polygon;
    polygon.reserve(5);
    auto hasExposedInterface = false;
    for (size_t corner = 0; corner < cell.positions.size(); ++corner) {
      auto floorElevation = cell.floor.evaluate(cell.positions[corner]);
      auto ceilingDistance =
          cell.ceiling.evaluate(cell.positions[corner]) - elevation;
      polygon.push_back(
          {cell.positions[corner], elevation - floorElevation,
           ceilingDistance});
      hasExposedInterface |= ceilingDistance > 0.0;
    }
    // A cell filled exactly to, or above, its ceiling remains part of the
    // Pool but has no exposed horizontal interface to draw.
    if (!hasExposedInterface) continue;
    polygon = ClipLiquidPolygon(std::move(polygon), true);
    polygon = ClipLiquidPolygon(std::move(polygon), false);
    if (polygon.size() < 3) continue;

    for (size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
      std::array<wp::Vector2, 3> positions{
          polygon[0].position, polygon[corner].position,
          polygon[corner + 1].position};
      if (TriangleArea(positions) <= 0.0) continue;
      result.push_back(
          {positions, float(elevation), cellIndex, cell.triangle.face});
    }
  }
  return result;
}

}  // namespace

LiquidState ComputeLiquidState(
    ArrangementResult const& arrangement,
    vector<ArrangementTriangle> const& triangles) {
  LiquidState result;
  auto faceCount = uint32_t(arrangement.faces.size());
  result.faceDepths.assign(faceCount, 0.0f);
  result.cells = BuildHydraulicCells(arrangement, triangles);
  result.poolElevations.assign(
      result.cells.size(), -numeric_limits<double>::infinity());
  if (faceCount == 0) return result;

  auto undistributed =
      ComputeUndistributedLiquidDepthsDouble(arrangement);
  auto cellCount = uint32_t(result.cells.size());
  if (cellCount == 0) return result;

  // Each cell receives its face's seeded depth over its own World-plane area.
  // This apportions every authored Primitive's conserved volume by surviving
  // cell area without pre-connecting cells merely because they share a face.
  vector<double> volumes(cellCount, 0.0);
  for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    auto face = result.cells[cellIndex].triangle.face;
    volumes[cellIndex] =
        undistributed[face] * result.cells[cellIndex].worldArea;
  }

  // Union-find over Hydraulic cells plus one permanent exterior-drain node.
  // Each group is a Pool: its cells share one surface elevation, conserve one
  // volume, and remember whether they have reached the exterior and emptied.
  auto drainNode = cellCount;
  auto nodeCount = cellCount + 1;
  vector<uint32_t> parent(nodeCount);
  vector<vector<uint32_t>> members(nodeCount);
  vector<double> groupVolume(nodeCount, 0.0);
  vector<double> groupLevel(nodeCount, -numeric_limits<double>::infinity());
  vector<bool> groupDrained(nodeCount, false);
  for (uint32_t node = 0; node < nodeCount; ++node) {
    parent[node] = node;
    if (node == drainNode) {
      groupDrained[node] = true;
      continue;
    }
    members[node] = {node};
    groupVolume[node] = volumes[node];
    if (volumes[node] > 0.0) {
      groupLevel[node] =
          SolveLiquidLevel(result.cells, members[node], volumes[node]);
    }
  }

  auto findRoot = [&parent](uint32_t node) {
    while (parent[node] != node) {
      parent[node] = parent[parent[node]];
      node = parent[node];
    }
    return node;
  };

  auto links = BuildHydraulicLinks(arrangement, result.cells);

  // Rising-level fill: repeatedly take the lowest sill whose liquid has
  // actually risen high enough to cross it, and resolve what crossing it
  // means. Two outcomes, and which one applies is decided by asking what
  // elevation the two pools would settle at as a single body of liquid:
  //
  //  - At or above the sill, the sill is submerged and they really are one
  //    body - two separate ponds becoming one lake the moment the rising
  //    surface tops the saddle between them. Merge them.
  //  - Below the sill, they are not: the higher pool is pouring over a saddle
  //    into somewhere lower and drier, and once its own surface falls back to
  //    the saddle the pouring stops. Only the liquid standing above the sill
  //    crosses, leaving the donor exactly brim-full at the sill and the two
  //    still separate.
  //
  // Every pass either merges two distinct groups - at most cellCount times -
  // or leaves a donor standing exactly at a sill, which cannot spill
  // over that sill again until something else pours into it, and can only
  // ever be poured into from strictly higher up. So this is a union-find walk
  // over liquid flowing downhill, not a convergence loop: there is no epsilon
  // and no iteration count anywhere in it.
  for (auto flowing = true; flowing;) {
    flowing = false;
    for (auto const& link : links) {
      auto root0 = findRoot(link.cell0);
      auto root1 = findRoot(link.drain ? drainNode : link.cell1);
      if (root0 == root1 ||
          max(groupLevel[root0], groupLevel[root1]) < link.sill) {
        continue;
      }

      auto drained = groupDrained[root0] || groupDrained[root1];
      auto combinedVolume = groupVolume[root0] + groupVolume[root1];
      auto combined = members[root0];
      combined.insert(
          combined.end(), members[root1].begin(), members[root1].end());
      // A pool that reaches the exterior empties completely, and so does
      // anything that later spills into it.
      auto combinedLevel =
          drained || combinedVolume <= 0.0
              ? -numeric_limits<double>::infinity()
              : SolveLiquidLevel(result.cells, combined, combinedVolume);

      if (drained || combinedLevel >= link.sill) {
        // One body of liquid. Merging changes its surface elevation, so
        // restart from the lowest sill rather than continuing down a stale
        // ordering.
        parent[root1] = root0;
        members[root0] = std::move(combined);
        members[root1].clear();
        groupVolume[root0] = drained ? 0.0 : combinedVolume;
        groupDrained[root0] = drained;
        groupLevel[root0] = combinedLevel;
        flowing = true;
        break;
      }

      // Not one body of liquid: a directed spill from the pool standing above
      // the sill into the one below it. Both pools survive, at their own two
      // elevations, with the donor left exactly at the sill.
      auto donor = groupLevel[root0] >= groupLevel[root1] ? root0 : root1;
      auto recipient = donor == root0 ? root1 : root0;
      auto retained =
          LiquidCapacityBelow(result.cells, members[donor], link.sill);
      auto spilled = groupVolume[donor] - retained;
      if (spilled <= 0.0) {
        // Already brim-full at this sill and holding nothing back. Re-running
        // LiquidCapacityBelow over an unchanged member list and an unchanged
        // sill reproduces the retained volume exactly, so this subtracts to
        // exactly zero rather than dribbling.
        continue;
      }
      groupVolume[donor] = retained;
      groupLevel[donor] = link.sill;
      groupVolume[recipient] += spilled;
      groupLevel[recipient] =
          groupVolume[recipient] > 0.0
              ? SolveLiquidLevel(
                    result.cells, members[recipient], groupVolume[recipient])
              : -numeric_limits<double>::infinity();
      flowing = true;
      break;
    }
  }

  for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    auto level = groupLevel[findRoot(cellIndex)];
    result.poolElevations[cellIndex] = level;

    // Legacy flat-world callers still receive one depth per face. There is no
    // truthful scalar depth for a sloped face, so its compatibility value
    // remains zero; position-based callers consume Hydraulic-cell state.
    auto faceIndex = result.cells[cellIndex].triangle.face;
    auto const& properties =
        arrangement.palette[arrangement.faces[faceIndex].paletteIndex];
    if (properties.floorZ.gradient != wp::Vector2::ZERO ||
        properties.ceilingZ.gradient != wp::Vector2::ZERO) {
      continue;
    }
    auto clearance = max(
        0.0, double(properties.ceilingZ.baseElevation) -
                 properties.floorZ.baseElevation);
    auto depth = float(clamp(
        level - properties.floorZ.baseElevation, 0.0, clearance));
    result.faceDepths[faceIndex] = max(result.faceDepths[faceIndex], depth);
  }
  result.surfaceTriangles =
      BuildLiquidSurfaceTriangles(result.cells, result.poolElevations);
  return result;
}

vector<float> ComputeLiquidLevels(ArrangementResult const& arrangement) {
  auto triangles = BuildArrangementTriangles(arrangement);
  return ComputeLiquidState(arrangement, triangles).faceDepths;
}

ArrangementWallOrientation OrientArrangementWall(
    ArrangementResult const& arrangement,
    ArrangementWall const& wall) {
  auto const& edge = arrangement.edges[wall.edge];
  auto const& face0 = arrangement.faces[edge.face[0]];
  auto const& face1 = arrangement.faces[edge.face[1]];
  auto const& fixed0 = arrangement.vertices[edge.v[0]];
  auto const& fixed1 = arrangement.vertices[edge.v[1]];
  wp::Vector2 source0{
      ToWorldCoordinate(fixed0.x), ToWorldCoordinate(fixed0.y)};
  wp::Vector2 source1{
      ToWorldCoordinate(fixed1.x), ToWorldCoordinate(fixed1.y)};
  auto sourceSpan = source1 - source0;

  ArrangementWallOrientation result{
      source0 + sourceSpan * wall.sourceEdgeParameter[0],
      source0 + sourceSpan * wall.sourceEdgeParameter[1],
      {},
      wall.bottomZ,
      wall.topZ};
  result.normal = (result.v1 - result.v0).normalisedCopy().perpendicular();

  auto face0IsFront = wall.frontFace == edge.face[0];
  if (wall.frontFace != edge.face[0] && wall.frontFace != edge.face[1]) {
    // Retain useful behavior for hand-authored test fixtures and older callers
    // that aggregate-initialize a wall without the derived frontFace field.
    auto midpoint = (result.v0 + result.v1) * 0.5f;
    switch (wall.kind) {
      case ArrangementWallKind::Border:
        face0IsFront = face0.solid;
        break;

      case ArrangementWallKind::FloorStep: {
        auto const& properties0 = arrangement.palette[face0.paletteIndex];
        auto const& properties1 = arrangement.palette[face1.paletteIndex];
        face0IsFront = properties0.floorZ.evaluate(midpoint) <
                       properties1.floorZ.evaluate(midpoint);
        break;
      }

      case ArrangementWallKind::CeilingStep: {
        auto const& properties0 = arrangement.palette[face0.paletteIndex];
        auto const& properties1 = arrangement.palette[face1.paletteIndex];
        face0IsFront = properties0.ceilingZ.evaluate(midpoint) >
                       properties1.ceilingZ.evaluate(midpoint);
        break;
      }

      default:
        throw std::logic_error("Unknown arrangement wall kind");
    }
  }

  if (!face0IsFront) {
    result.normal = -result.normal;
    std::swap(result.v0, result.v1);
    std::swap(result.bottomZ[0], result.bottomZ[1]);
    std::swap(result.topZ[0], result.topZ[1]);
  }
  return result;
}

ArrangementWallSurface BuildArrangementWallSurface(
    ArrangementResult const& arrangement,
    ArrangementWall const& wall) {
  auto orientation = OrientArrangementWall(arrangement, wall);
  auto startHeight = orientation.topZ[0] - orientation.bottomZ[0];
  auto endHeight = orientation.topZ[1] - orientation.bottomZ[1];
  ArrangementWallSurface result;
  if (startHeight > 0.0f && endHeight > 0.0f) {
    result.vertices = {
        ArrangementWallSurfaceVertex{
            orientation.v0, orientation.bottomZ[0], 0, false},
        ArrangementWallSurfaceVertex{
            orientation.v0, orientation.topZ[0], 0, true},
        ArrangementWallSurfaceVertex{
            orientation.v1, orientation.topZ[1], 1, true},
        ArrangementWallSurfaceVertex{
            orientation.v1, orientation.bottomZ[1], 1, false}};
    result.vertexCount = 4;
  } else if (startHeight > 0.0f) {
    result.vertices = {
        ArrangementWallSurfaceVertex{
            orientation.v0, orientation.bottomZ[0], 0, false},
        ArrangementWallSurfaceVertex{
            orientation.v0, orientation.topZ[0], 0, true},
        ArrangementWallSurfaceVertex{
            orientation.v1, orientation.topZ[1], 1, true}};
    result.vertexCount = 3;
  } else if (endHeight > 0.0f) {
    result.vertices = {
        ArrangementWallSurfaceVertex{
            orientation.v0, orientation.topZ[0], 0, true},
        ArrangementWallSurfaceVertex{
            orientation.v1, orientation.topZ[1], 1, true},
        ArrangementWallSurfaceVertex{
            orientation.v1, orientation.bottomZ[1], 1, false}};
    result.vertexCount = 3;
  }
  return result;
}

}  // namespace bw::core::arr
