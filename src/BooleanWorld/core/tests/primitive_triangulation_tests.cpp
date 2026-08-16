#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/MeshPrimitive.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::ClosedPolygon rectangle(float left, float bottom, float right, float top) {
  return {{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}};
}

bool isInRectangle(bw::core::Vertex const& vertex, float left, float bottom, float right, float top) {
  return vertex.p.x >= left && vertex.p.x <= right &&
         vertex.p.y >= bottom && vertex.p.y <= top;
}

void triangulatesEachComplexPolygonIndependently() {
  std::vector<bw::core::ComplexPolygon> complexPolygons{
      {rectangle(0.0f, 0.0f, 1.0f, 1.0f)},
      {rectangle(10.0f, 10.0f, 11.0f, 11.0f)}};
  auto primitive = std::unique_ptr<bw::core::MeshPrimitive>(
      bw::core::MeshPrimitive::fromComplexPolygons(
          bw::core::Primitive::Operation::Union,
          bw::core::Primitive::FillRule::NonZero,
          complexPolygons));
  primitive->setId(42);
  primitive->updateVertexPositions();

  auto triangulation = primitive->triangulate(false);

  require(triangulation.tris.size() == 4,
          "two rectangular complex polygons did not produce four triangles: got " +
              std::to_string(triangulation.tris.size()));
  for (auto const& triangle : triangulation.tris) {
    require(triangle.primitiveIndex == 42,
            "triangle did not retain its primitive index");
    bool const inFirstPolygon = isInRectangle(triangle.v[0], 0.0f, 0.0f, 1.0f, 1.0f) &&
                                isInRectangle(triangle.v[1], 0.0f, 0.0f, 1.0f, 1.0f) &&
                                isInRectangle(triangle.v[2], 0.0f, 0.0f, 1.0f, 1.0f);
    bool const inSecondPolygon = isInRectangle(triangle.v[0], 10.0f, 10.0f, 11.0f, 11.0f) &&
                                 isInRectangle(triangle.v[1], 10.0f, 10.0f, 11.0f, 11.0f) &&
                                 isInRectangle(triangle.v[2], 10.0f, 10.0f, 11.0f, 11.0f);
    require(inFirstPolygon || inSecondPolygon,
            "triangle joined vertices from separate complex polygons: (" +
                std::to_string(triangle.v[0].p.x) + ", " + std::to_string(triangle.v[0].p.y) + "), (" +
                std::to_string(triangle.v[1].p.x) + ", " + std::to_string(triangle.v[1].p.y) + "), (" +
                std::to_string(triangle.v[2].p.x) + ", " + std::to_string(triangle.v[2].p.y) + ")");
  }
}

}  // namespace

int main() {
  try {
    triangulatesEachComplexPolygonIndependently();
    std::cout << "Primitive triangulates complex polygons independently\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
