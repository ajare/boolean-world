#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/AnimatedProperty.h>
#include <core/CoreException.h>
#include <core/Interpolator.h>
#include <core/RectanglePolygon.h>
#include <core/VertexTransformer.h>
#include <core/World.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void requireRuntimeError(Function&& function, char const* message) {
  try {
    function();
  } catch (std::runtime_error const& error) {
    require(std::string(error.what()).contains("not found in AccelerationGrid"),
            "rethrow did not preserve the exception message");
    return;
  } catch (std::exception const&) {
    throw std::runtime_error(message);
  }
  throw std::runtime_error("expected an exception");
}

template <typename Function>
void requireIndexOutOfRange(Function&& function, char const* message) {
  try {
    function();
  } catch (bw::core::CoreException const& error) {
    require(std::string(error.what()) == "index out of range", message);
    return;
  }
  throw std::runtime_error(message);
}

bw::core::RectanglePolygon* makeRectangle() {
  auto* rectangle = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rectangle->setSize(10.0f, 10.0f);
  return rectangle;
}

void worldReplacementPreservesExceptionType() {
  bw::core::World world(100.0f, 10.0f);
  auto* resident = makeRectangle();
  world.addPrimitive(resident);
  resident->setId(99);

  auto replacement = std::unique_ptr<bw::core::RectanglePolygon>(makeRectangle());
  requireRuntimeError(
      [&] { world.replacePrimitive(0, replacement.get()); },
      "replacePrimitive sliced the acceleration-grid exception");
}

void coreExceptionsArePortableAndDescriptive() {
  bw::core::Interpolator<float> interpolator;
  try {
    interpolator.setPoints({{1.0f, 1.0f}, {0.0f, 0.0f}});
  } catch (bw::core::CoreException const& error) {
    require(std::string(error.what()) == "Interpolator points not ascending in time",
            "CoreException did not preserve its message");
    return;
  }
  throw std::runtime_error("Interpolator did not throw CoreException");
}

void emptyTransformAndEventRemovalIsRejected() {
  bw::core::VertexTransformer transformer;
  bw::core::AnimatedProperty property;

  requireIndexOutOfRange(
      [&] { transformer.removeTransform(bw::core::VertexTransformer::Key::Scale, 0); },
      "removing a transform from an empty collection did not throw");
  requireIndexOutOfRange(
      [&] { property.removeEvent(0); },
      "removing an event from an empty collection did not throw");
}

void outOfRangeRemovalIsRejectedWithoutChangingCollections() {
  bw::core::VertexTransformer transformer;
  transformer.addTransform(bw::core::VertexTransformer::Key::Scale, bw::core::tTransform::makeZero());
  bw::core::AnimatedProperty property;
  property.addEvent(1, bw::core::AnimatedPropertyEventTriggerType::Up, 0.25f);

  requireIndexOutOfRange(
      [&] { transformer.removeTransform(bw::core::VertexTransformer::Key::Scale, 1); },
      "removing an out-of-range transform did not throw");
  require(transformer.getScaleTransforms().size() == 1,
          "removing an out-of-range transform changed the collection");
  requireIndexOutOfRange(
      [&] { property.removeEvent(1); },
      "removing an out-of-range event did not throw");
  require(property.getNumEvents() == 1, "removing an out-of-range event changed the collection");
}

void validRemovalStillRemovesTheRequestedItem() {
  bw::core::VertexTransformer transformer;
  transformer.addTransform(bw::core::VertexTransformer::Key::Scale, bw::core::tTransform::makeZero());
  transformer.addTransform(bw::core::VertexTransformer::Key::Scale, bw::core::tTransform::makeOne());
  transformer.removeTransform(bw::core::VertexTransformer::Key::Scale, 0);
  require(transformer.getScaleTransforms().size() == 1, "removing a transform did not shrink the collection");

  bw::core::AnimatedProperty property;
  property.addEvent(1, bw::core::AnimatedPropertyEventTriggerType::Up, 0.25f);
  property.addEvent(2, bw::core::AnimatedPropertyEventTriggerType::Down, 0.75f);
  property.removeEvent(0);
  require(property.getNumEvents() == 1, "removing an event did not shrink the collection");
  require(property.getEvents()[0].eventType == 2, "removing an event did not preserve the remaining event");
}

}  // namespace

int main() {
  try {
    worldReplacementPreservesExceptionType();
    coreExceptionsArePortableAndDescriptive();
    emptyTransformAndEventRemovalIsRejected();
    outOfRangeRemovalIsRejectedWithoutChangingCollections();
    validRemovalStillRemovesTheRequestedItem();
    std::cout << "Exception regression coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
