#include <concepts>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <willpower/common/BoundingConvexPolygon.h>
#include <willpower/common/MathsUtils.h>
#include <willpower/common/Vector2.h>

namespace {

template <typename T>
concept HasDistanceToRay = requires(T point, T const& origin, T const& direction) {
  { point.distanceToRay(origin, direction) } -> std::convertible_to<float>;
};

template <typename T>
concept HasConvexPolygonArea = requires(std::vector<wp::Vector2> const& vertices) {
  T::convexPolygonArea(vertices);
};

template <typename T>
concept HasPointInConvexPolygon = requires(wp::Vector2 const& point,
                                           std::vector<wp::Vector2> const& vertices) {
  T::pointInConvexPolygon(point, vertices);
};

template <typename T>
concept HasTriangleIntersectsConvexPolygon = requires(
    wp::Vector2 const& vertex, std::vector<wp::Vector2> const& vertices) {
  T::triangleIntersectsConvexPolygon(vertex, vertex, vertex, vertices);
};

template <typename T>
concept HasConvexPolygonIntersectsConvexPolygon = requires(
    std::vector<wp::Vector2> const& vertices) {
  T::convexPolygonIntersectsConvexPolygon(vertices, vertices);
};

template <typename T>
concept HasPointInside = requires(T const& polygon, wp::Vector2 const& point) {
  polygon.pointInside(point);
};

static_assert(!HasDistanceToRay<wp::Vector2>);
static_assert(!HasConvexPolygonArea<wp::MathsUtils>);
static_assert(!HasPointInConvexPolygon<wp::MathsUtils>);
static_assert(!HasTriangleIntersectsConvexPolygon<wp::MathsUtils>);
static_assert(!HasConvexPolygonIntersectsConvexPolygon<wp::MathsUtils>);
static_assert(!HasPointInside<wp::BoundingConvexPolygon>);

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void auditedGeometryHelpersAreNotPublicApis() {
  require(!HasDistanceToRay<wp::Vector2>,
          "Vector2 must not expose the broken ray-distance helper");
  require(!HasConvexPolygonArea<wp::MathsUtils>,
          "MathsUtils must not expose the broken convex-area helper");
  require(!HasPointInConvexPolygon<wp::MathsUtils>,
          "MathsUtils must not expose the broken convex-polygon helper");
  require(!HasTriangleIntersectsConvexPolygon<wp::MathsUtils>,
          "MathsUtils must not expose underflowing convex-polygon helpers");
  require(!HasConvexPolygonIntersectsConvexPolygon<wp::MathsUtils>,
          "MathsUtils must not expose underflowing convex-polygon helpers");
  require(!HasPointInside<wp::BoundingConvexPolygon>,
          "BoundingConvexPolygon must not retain a facade for the removed helper");
}

}  // namespace

int main() {
  try {
    auditedGeometryHelpersAreNotPublicApis();
    std::cout << "Legacy geometry helper removal passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
