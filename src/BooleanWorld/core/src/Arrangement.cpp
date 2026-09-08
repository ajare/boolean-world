#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
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
  auto hierarchy = BuildPolygonHierarchy(graph, cycles);
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
      if (runSolid &&
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
      array<float, 2> result;
      for (size_t endpoint = 0; endpoint < result.size(); ++endpoint) {
        result[endpoint] = elevation.evaluate(endpointPositions[endpoint]);
      }
      return result;
    };
    auto appendWall = [&](uint16_t paletteIndex, ArrangementWallKind kind,
                          array<float, 2> const& bottomZ,
                          array<float, 2> const& topZ, float clearance) {
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
           topZ});
    };

    if (face0.solid != face1.solid) {
      auto const& solidFace = face0.solid ? face0 : face1;
      auto const& emptyFace = face0.solid ? face1 : face0;
      auto const& properties =
          arrangement.palette[solidFace.paletteIndex];
      // An authored Difference contributes no solid Face of its own, but its
      // boundary creates the newly exposed wall. Property-transparent
      // structural Differences instead leave ownership with the surviving
      // solid Face. In either case that Face supplies the vertical span.
      auto paletteIndex =
          emptyFace.operation == bw::core::Primitive::Operation::Difference &&
                  emptyFace.contributesProperties
              ? emptyFace.paletteIndex
              : solidFace.paletteIndex;
      auto bottomZ = evaluate(properties.floorZ);
      auto topZ = evaluate(properties.ceilingZ);
      appendWall(
          paletteIndex, ArrangementWallKind::Border, bottomZ, topZ,
          min(topZ[0] - bottomZ[0], topZ[1] - bottomZ[1]));
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
    // The smallest headroom available anywhere on an affine source edge is
    // attained at an endpoint. Retain that conservative scalar for existing
    // traversal while preserving the evaluated endpoint geometry.
    auto clearance = min(
        min(ceiling0[0], ceiling1[0]) - max(floor0[0], floor1[0]),
        min(ceiling0[1], ceiling1[1]) - max(floor0[1], floor1[1]));
    if (properties0.floorZ != properties1.floorZ) {
      auto const& higherFloorFace =
          properties0.floorZ > properties1.floorZ ? face0 : face1;
      array<float, 2> bottomZ{
          min(floor0[0], floor1[0]), min(floor0[1], floor1[1])};
      array<float, 2> topZ{
          max(floor0[0], floor1[0]), max(floor0[1], floor1[1])};
      appendWall(
          higherFloorFace.paletteIndex, ArrangementWallKind::FloorStep,
          bottomZ, topZ, clearance);
    }
    if (properties0.ceilingZ != properties1.ceilingZ) {
      auto const& lowerCeilingFace =
          properties0.ceilingZ < properties1.ceilingZ ? face0 : face1;
      array<float, 2> bottomZ{
          min(ceiling0[0], ceiling1[0]), min(ceiling0[1], ceiling1[1])};
      array<float, 2> topZ{
          max(ceiling0[0], ceiling1[0]), max(ceiling0[1], ceiling1[1])};
      appendWall(
          lowerCeilingFace.paletteIndex, ArrangementWallKind::CeilingStep,
          bottomZ, topZ, clearance);
    }
  }
  return walls;
}

vector<LiquidAdjacency> BuildLiquidAdjacency(ArrangementResult const& arrangement) {
  vector<LiquidAdjacency> result;
  for (auto const& edge : arrangement.edges) {
    auto f0 = edge.face[0];
    auto f1 = edge.face[1];
    if (f0 == f1) {
      continue;
    }
    auto const& face0 = arrangement.faces[f0];
    auto const& face1 = arrangement.faces[f1];

    // The unbounded exterior face (index 0) is never solid itself; any other
    // pair must both be real rooms (solid, per BuildArrangementTriangles'
    // same convention) to equilibrate together.
    auto drain = f0 == 0 || f1 == 0;
    if (drain) {
      auto const& roomFace = f0 == 0 ? face1 : face0;
      // This edge is exactly the Border wall BuildArrangementWalls would
      // build here, and that wall collides by default - the same as any
      // other Border wall - unless explicitly authored not to. A solid wall
      // that already blocks the player blocks liquid the same way, so only
      // an edge explicitly marked non-colliding is actually open to the
      // Arrangement's unbounded exterior; an ordinary outer wall is not a
      // drain just because nothing is authored beyond it.
      if (!roomFace.solid || edge.collidesOverride.value_or(true)) {
        continue;
      }
    } else if (!face0.solid || !face1.solid) {
      continue;
    }

    if (!drain) {
      auto const& properties0 = arrangement.palette[face0.paletteIndex];
      auto const& properties1 = arrangement.palette[face1.paletteIndex];
      // Same headroom computation BuildArrangementWalls uses for player
      // movement, rather than a second connectivity notion.
      auto clearance = min(properties0.ceilingZ, properties1.ceilingZ) -
                       max(properties0.floorZ, properties1.floorZ);
      if (clearance == 0.0f) {
        continue;
      }
    }

    result.push_back(
        {min(f0, f1), max(f0, f1), drain});
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

vector<float> ComputeUndistributedLiquidDepths(
    ArrangementResult const& arrangement) {
  auto faceCount = uint32_t(arrangement.faces.size());
  vector<float> depths(faceCount, 0.0f);
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
    depths[faceIndex] = float(depth);
  }
  return depths;
}

namespace {
// One face's hydrology inputs, gathered once so the fill below never has to
// go back to the palette or recompute an area.
struct FaceLiquid {
  double floorZ{0};
  double ceilingZ{0};
  double area{0};
};

// Volume held by these faces if the liquid surface sat at elevation z. Each
// face holds area * (z - floorZ), never below zero and never past its own
// ceiling, so this is continuous, non-decreasing and piecewise linear in z.
double LiquidCapacityBelow(
    vector<FaceLiquid> const& liquid,
    vector<uint32_t> const& faces,
    double z) {
  double total = 0.0;
  for (auto faceIndex : faces) {
    auto const& face = liquid[faceIndex];
    auto clearance = max(0.0, face.ceilingZ - face.floorZ);
    total += face.area * clamp(z - face.floorZ, 0.0, clearance);
  }
  return total;
}

// The elevation at which `volume` settles across these faces, found exactly:
// LiquidCapacityBelow bends only at a floor or a ceiling, so the answer is the
// linear interpolation inside the single segment between consecutive
// breakpoints that brackets the volume - no epsilon, no iteration. A volume
// exceeding every face's combined capacity settles at the highest ceiling,
// which caps each member face and silently discards the excess.
double SolveLiquidLevel(
    vector<FaceLiquid> const& liquid,
    vector<uint32_t> const& faces,
    double volume) {
  vector<double> breakpoints;
  breakpoints.reserve(faces.size() * 2);
  for (auto faceIndex : faces) {
    auto const& face = liquid[faceIndex];
    breakpoints.push_back(face.floorZ);
    breakpoints.push_back(max(face.floorZ, face.ceilingZ));
  }
  sort(breakpoints.begin(), breakpoints.end());
  breakpoints.erase(
      unique(breakpoints.begin(), breakpoints.end()), breakpoints.end());
  if (breakpoints.empty()) {
    return -numeric_limits<double>::infinity();
  }

  auto lowerCapacity = 0.0;
  for (size_t i = 1; i < breakpoints.size(); ++i) {
    auto upperCapacity = LiquidCapacityBelow(liquid, faces, breakpoints[i]);
    if (upperCapacity >= volume) {
      auto slope = (upperCapacity - lowerCapacity) /
                   (breakpoints[i] - breakpoints[i - 1]);
      if (slope > 0.0) {
        return breakpoints[i - 1] + (volume - lowerCapacity) / slope;
      }
    }
    lowerCapacity = upperCapacity;
  }
  return breakpoints.back();
}

// One liquid-adjacency resolved into the elevation liquid has to reach before
// it can cross: the higher of the two floors, since the lower face's liquid
// only reaches the higher face once it is deep enough to top that face's
// floor. The exterior drain's floor is negative infinity, so a drain link's
// sill is simply the bordering face's own floor.
struct LiquidLink {
  double sill{0};
  uint32_t face0{0};
  uint32_t face1{0};
};
}  // namespace

vector<float> ComputeLiquidLevels(ArrangementResult const& arrangement) {
  auto faceCount = uint32_t(arrangement.faces.size());
  vector<float> depths(faceCount, 0.0f);
  if (faceCount == 0) {
    return depths;
  }

  auto undistributed = ComputeUndistributedLiquidDepths(arrangement);

  // Face zero, the unbounded exterior, is the permanent drain: it never holds
  // liquid and its entry stays zeroed.
  vector<FaceLiquid> liquid(faceCount);
  vector<double> volumes(faceCount, 0.0);
  for (uint32_t faceIndex = 1; faceIndex < faceCount; ++faceIndex) {
    auto const& face = arrangement.faces[faceIndex];
    if (!face.solid) {
      continue;
    }
    auto const& properties = arrangement.palette[face.paletteIndex];
    liquid[faceIndex] = {
        double(properties.floorZ), double(properties.ceilingZ),
        max(0.0, FaceArea(face, arrangement))};
    volumes[faceIndex] = double(undistributed[faceIndex]) * liquid[faceIndex].area;
  }

  // Union-find over faces. Each group is a pool: the faces sharing one
  // surface elevation, the volume they hold between them, and whether that
  // pool has reached the exterior and emptied.
  vector<uint32_t> parent(faceCount);
  vector<vector<uint32_t>> members(faceCount);
  vector<double> groupVolume(faceCount, 0.0);
  vector<double> groupLevel(faceCount, -numeric_limits<double>::infinity());
  vector<bool> groupDrained(faceCount, false);
  for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    parent[faceIndex] = faceIndex;
    if (faceIndex == 0) {
      groupDrained[0] = true;
      continue;
    }
    if (!arrangement.faces[faceIndex].solid) {
      continue;
    }
    members[faceIndex].push_back(faceIndex);
    groupVolume[faceIndex] = volumes[faceIndex];
    if (volumes[faceIndex] > 0.0) {
      groupLevel[faceIndex] =
          SolveLiquidLevel(liquid, members[faceIndex], volumes[faceIndex]);
    }
  }

  auto findRoot = [&parent](uint32_t faceIndex) {
    while (parent[faceIndex] != faceIndex) {
      parent[faceIndex] = parent[parent[faceIndex]];
      faceIndex = parent[faceIndex];
    }
    return faceIndex;
  };

  vector<LiquidLink> links;
  for (auto const& adjacency : BuildLiquidAdjacency(arrangement)) {
    auto sill = adjacency.drain
                    ? liquid[adjacency.face0 == 0 ? adjacency.face1
                                                  : adjacency.face0]
                          .floorZ
                    : max(liquid[adjacency.face0].floorZ,
                          liquid[adjacency.face1].floorZ);
    links.push_back({sill, adjacency.face0, adjacency.face1});
  }
  sort(links.begin(), links.end(), [](auto const& a, auto const& b) {
    return tie(a.sill, a.face0, a.face1) < tie(b.sill, b.face0, b.face1);
  });

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
  // Every pass either merges two distinct groups - at most faceCount - 1
  // times - or leaves a donor standing exactly at a sill, which cannot spill
  // over that sill again until something else pours into it, and can only
  // ever be poured into from strictly higher up. So this is a union-find walk
  // over liquid flowing downhill, not a convergence loop: there is no epsilon
  // and no iteration count anywhere in it.
  for (auto flowing = true; flowing;) {
    flowing = false;
    for (auto const& link : links) {
      auto root0 = findRoot(link.face0);
      auto root1 = findRoot(link.face1);
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
              : SolveLiquidLevel(liquid, combined, combinedVolume);

      if (drained || combinedLevel >= link.sill) {
        // One body of liquid. Merging changes its surface elevation, so
        // restart from the lowest sill rather than continuing down a stale
        // ordering.
        parent[root1] = root0;
        members[root0] = std::move(combined);
        members[root1].clear();
        groupVolume[root0] = combinedVolume;
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
      auto retained = LiquidCapacityBelow(liquid, members[donor], link.sill);
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
                    liquid, members[recipient], groupVolume[recipient])
              : -numeric_limits<double>::infinity();
      flowing = true;
      break;
    }
  }

  for (uint32_t faceIndex = 1; faceIndex < faceCount; ++faceIndex) {
    if (!arrangement.faces[faceIndex].solid) {
      continue;
    }
    auto level = groupLevel[findRoot(faceIndex)];
    auto const& face = liquid[faceIndex];
    auto clearance = max(0.0, face.ceilingZ - face.floorZ);
    depths[faceIndex] = float(clamp(level - face.floorZ, 0.0, clearance));
  }
  return depths;
}

ArrangementWallOrientation OrientArrangementWall(
    ArrangementResult const& arrangement,
    ArrangementWall const& wall) {
  auto const& edge = arrangement.edges[wall.edge];
  auto const& face0 = arrangement.faces[edge.face[0]];
  auto const& face1 = arrangement.faces[edge.face[1]];
  auto const& fixed0 = arrangement.vertices[edge.v[0]];
  auto const& fixed1 = arrangement.vertices[edge.v[1]];

  ArrangementWallOrientation result{
      {ToWorldCoordinate(fixed0.x), ToWorldCoordinate(fixed0.y)},
      {ToWorldCoordinate(fixed1.x), ToWorldCoordinate(fixed1.y)},
      {},
      wall.bottomZ,
      wall.topZ};
  result.normal = (result.v1 - result.v0).normalisedCopy().perpendicular();

  bool face0IsFront = false;
  switch (wall.kind) {
    case ArrangementWallKind::Border:
      face0IsFront = face0.solid;
      break;

    case ArrangementWallKind::FloorStep: {
      auto const& properties0 = arrangement.palette[face0.paletteIndex];
      auto const& properties1 = arrangement.palette[face1.paletteIndex];
      face0IsFront = properties0.floorZ < properties1.floorZ;
      break;
    }

    case ArrangementWallKind::CeilingStep: {
      auto const& properties0 = arrangement.palette[face0.paletteIndex];
      auto const& properties1 = arrangement.palette[face1.paletteIndex];
      face0IsFront = properties0.ceilingZ > properties1.ceilingZ;
      break;
    }

    default:
      throw std::logic_error("Unknown arrangement wall kind");
  }

  if (!face0IsFront) {
    result.normal = -result.normal;
    std::swap(result.v0, result.v1);
    std::swap(result.bottomZ[0], result.bottomZ[1]);
    std::swap(result.topZ[0], result.topZ[1]);
  }
  return result;
}

}  // namespace bw::core::arr
