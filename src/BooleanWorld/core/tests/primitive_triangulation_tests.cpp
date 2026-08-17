#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/MeshPrimitive.h>

namespace {

std::atomic_size_t gAllocationCount;

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

void triangulationDoesNotCopyPrimitiveVertices() {
  std::vector<bw::core::ComplexPolygon> complexPolygons;
  constexpr int complexPolygonCount = 32;
  for (int i = 0; i < complexPolygonCount; ++i) {
    auto const left = static_cast<float>(i * 2);
    complexPolygons.push_back({rectangle(left, 0.0f, left + 1.0f, 1.0f)});
  }
  auto primitive = std::unique_ptr<bw::core::MeshPrimitive>(
      bw::core::MeshPrimitive::fromComplexPolygons(
          bw::core::Primitive::Operation::Union,
          bw::core::Primitive::FillRule::NonZero,
          complexPolygons));
  primitive->updateVertexPositions();

  gAllocationCount = 0;
  auto triangulation = primitive->triangulate(false);
  auto const allocationCount = gAllocationCount.load();

  require(triangulation.tris.size() == complexPolygonCount * 2,
          "triangulation did not retain every complex polygon");
  require(allocationCount <= 650,
          "triangulation copied stored primitive vertices: " + std::to_string(allocationCount));
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

void* operator new(std::size_t size) {
  if (auto* memory = std::malloc(size)) {
    gAllocationCount.fetch_add(1, std::memory_order_relaxed);
    return memory;
  }
  throw std::bad_alloc();
}

void operator delete(void* memory) noexcept {
  std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
  std::free(memory);
}

int main() {
  try {
    triangulationDoesNotCopyPrimitiveVertices();
    triangulatesEachComplexPolygonIndependently();
    std::cout << "Primitive triangulates complex polygons independently\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
