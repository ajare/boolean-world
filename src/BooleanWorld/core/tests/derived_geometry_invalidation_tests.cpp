#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <core/MeshPrimitive.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(float actual, float expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon,
          message + ": expected " + std::to_string(expected) +
              ", got " + std::to_string(actual));
}

class MutationProbe final : public bw::core::VertexTransformerObject {
public:
  mutable uint32_t invalidationCount{0};
  mutable bool recalculatedBounds{false};
  mutable bool notifiedWorld{false};

protected:
  void invalidatePostTransform(bool recalculateBounds, bool notifyWorld) const override {
    ++invalidationCount;
    recalculatedBounds = recalculateBounds;
    notifiedWorld = notifyWorld;
  }

  void notifyWorldChanged() const override {
  }
};

using Key = bw::core::VertexTransformer::Key;

static_assert(std::is_same_v<
                  decltype(std::declval<MutationProbe&>().getAnimationInterpolator(Key::Scale)),
                  bw::core::Interpolator<float> const&>,
              "read access must not allow animator mutation without a scope");

bw::core::MeshPrimitive makeAsymmetricMesh() {
  std::vector<bw::core::ComplexPolygon> const polygons{
      {{{{0.0f, 0.0f}}, {{2.0f, 0.0f}}, {{0.0f, 1.0f}}}}};
  auto primitive = std::unique_ptr<bw::core::MeshPrimitive>(
      bw::core::MeshPrimitive::fromComplexPolygons(
          bw::core::Primitive::Operation::Union, polygons));
  {
    auto mutation = primitive->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::Scale)
        .setPoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
  }
  primitive->setSize(1.0f, 1.0f);
  primitive->setFlags(primitive->getFlags() | BW_PRIMITIVE_EXACT_BOUNDS_FLAG);
  primitive->setPosition({3.0f, 4.0f});
  primitive->updateVertexPositions();
  return *primitive;
}

void animatorMutationScopeInvalidatesOnce() {
  MutationProbe probe;

  {
    auto mutation = probe.mutate();
    mutation.animation(Key::Scale).setPoints({{0.0f, 2.0f}, {1.0f, 2.0f}});
    mutation.influence(Key::Scale).setPoints({{0.0f, 0.5f}, {1.0f, 0.5f}});
    require(probe.invalidationCount == 0,
            "animator mutation invalidated before the scope completed");
  }

  require(probe.invalidationCount == 1,
          "one animator mutation scope did not produce one invalidation");
  require(probe.recalculatedBounds,
          "animator mutation did not request bounds recalculation");
  require(probe.notifiedWorld,
          "animator mutation did not notify the world");
  requireNear(probe.getAnimationInterpolator(Key::Scale).getValue(0.0f), 2.0f,
              "animation interpolator mutation was not retained");
  requireNear(probe.getInfluenceInterpolator(Key::Scale).getValue(0.0f), 0.5f,
              "influence interpolator mutation was not retained");
}

void animatorMutationScopeInvalidatesDuringUnwinding() {
  MutationProbe probe;

  try {
    auto mutation = probe.mutate();
    mutation.animation(Key::Angle).setPoints({{0.0f, 45.0f}, {1.0f, 45.0f}});
    throw std::runtime_error("abort edit");
  } catch (std::runtime_error const&) {
  }

  require(probe.invalidationCount == 1,
          "animator mutation was not invalidated while unwinding");
  requireNear(probe.getAnimationInterpolator(Key::Angle).getValue(0.0f), 45.0f,
              "animation mutation was lost while unwinding");
}

void orientationInvalidatesExactBounds() {
  auto primitive = makeAsymmetricMesh();
  auto const originalBounds = primitive.getBounds();
  primitive.setOrientation(90.0f);

  auto const& bounds = primitive.getBounds();
  requireNear(bounds.getWidth(), originalBounds.getHeight(),
              "orientation did not refresh the exact bounds width");
  requireNear(bounds.getHeight(), originalBounds.getWidth(),
              "orientation did not refresh the exact bounds height");
}

void rotatedCopyRefreshesTransformedVertices() {
  auto source = makeAsymmetricMesh();
  auto rotated = std::unique_ptr<bw::core::Primitive>(source.rotatedCopy(90.0f));

  auto const& vertices = rotated->getVertices();
  require(vertices.size() == 1 && vertices[0].size() == 1 &&
              vertices[0][0].size() == 3,
          "rotated copy did not retain its contour");

  auto const& sourceContour = source.getVertices()[0][0];
  auto const& contour = vertices[0][0];
  for (size_t i = 0; i < contour.size(); ++i) {
    auto const expected = sourceContour[i].p.rotatedClockwiseCopy(90.0f);
    requireNear(contour[i].p.x, expected.x,
                "rotated copy retained a source vertex x coordinate");
    requireNear(contour[i].p.y, expected.y,
                "rotated copy retained a source vertex y coordinate");
  }
}

}  // namespace

int main() {
  try {
    animatorMutationScopeInvalidatesOnce();
    animatorMutationScopeInvalidatesDuringUnwinding();
    orientationInvalidatesExactBounds();
    rotatedCopyRefreshesTransformedVertices();
    std::cout << "Scoped animator and primitive mutations invalidate derived geometry\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
