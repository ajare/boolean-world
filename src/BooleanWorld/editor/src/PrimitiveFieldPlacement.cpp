#include "PrimitiveFieldPlacement.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <core/Defines.h>
#include <core/Primitive.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>

#include "Actions.h"
#include "AppHelpers.h"
#include "Document.h"
#include "Settings.h"
#include "UiHelpers.h"
#include "Undo.h"

namespace editor {
namespace {

bool finitePoint(wp::Vector2 const& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

void validatePreview(
    bw::core::PrimitiveFieldLayout const& layout,
    std::vector<PrimitiveFieldRectanglePreview> const& rectangles) {
  if (layout.sites.empty() || layout.sites.size() != layout.cells.size() ||
      rectangles.size() != layout.sites.size()) {
    throw std::runtime_error(
        "Placement requires one complete Rectangle preview per Voronoi cell.");
  }

  auto const tolerance = bw::core::PrimitiveFieldNumericTolerance;
  for (size_t i = 0; i < rectangles.size(); ++i) {
    auto const& site = layout.sites[i];
    auto const& cell = layout.cells[i];
    auto const& rectangle = rectangles[i];
    if (!finitePoint(site) || cell.vertices.size() < 3 ||
        rectangle.position != site || !std::isfinite(rectangle.size) ||
        rectangle.size <= 0.0f || !std::isfinite(rectangle.angle) ||
        rectangle.angle < 0.0f || rectangle.angle >= 360.0f) {
      throw std::runtime_error("The Rectangle preview is incomplete or invalid.");
    }

    for (auto const& vertex : cell.vertices) {
      if (!finitePoint(vertex)) {
        throw std::runtime_error("A Voronoi cell contains a non-finite vertex.");
      }
      auto local = vertex - site;
      local.rotateClockwise(rectangle.angle);
      auto halfWidth = rectangle.size * 0.5f;
      if (std::abs(local.x) > halfWidth + tolerance ||
          std::abs(local.y) >
              halfWidth / PrimitiveFieldRectangleXyRatio + tolerance) {
        throw std::runtime_error(
            "A fitted Rectangle does not contain its complete Voronoi cell.");
      }
    }
  }
}

std::vector<std::unique_ptr<bw::core::Primitive>> buildBatch(
    std::vector<PrimitiveFieldRectanglePreview> const& rectangles) {
  std::vector<std::unique_ptr<bw::core::Primitive>> batch;
  batch.reserve(rectangles.size());
  for (auto const& rectangle : rectangles) {
    auto primitive = std::make_unique<bw::core::RectanglePolygon>(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        PrimitiveFieldRectangleXyRatio);
    _setPrimitiveParameters(
        primitive.get(), 0, 0, rectangle.position, wp::Vector2::ZERO,
        rectangle.size, rectangle.angle);
    setPrimitiveDefaultMaterials(primitive.get());

    auto const& angle = primitive->getAnimationInterpolator(
        bw::core::VertexTransformer::Key::Angle);
    auto const& points = angle.getPoints();
    if (primitive->getOperation() != bw::core::Primitive::Operation::Union ||
        primitive->getFillRule() != bw::core::Primitive::FillRule::NonZero ||
        primitive->getLayer() != 0 || primitive->getPriority() != 0 ||
        primitive->getOrientation() != 0.0f ||
        primitive->getPosition() != rectangle.position ||
        primitive->getSize() !=
            wp::Vector2{rectangle.size, rectangle.size} ||
        points.size() != 2 || points[0] != std::pair{0.0f, rectangle.angle} ||
        points[1] != std::pair{1.0f, rectangle.angle} ||
        !primitive->isStatic()) {
      throw std::runtime_error(
          "A Rectangle could not be initialized with editor defaults.");
    }
    batch.push_back(std::move(primitive));
  }
  return batch;
}

}  // namespace

PrimitiveFieldPlacementResult placePrimitiveField(
    Document* document,
    bw::core::PrimitiveFieldLayout const& layout,
    std::vector<PrimitiveFieldRectanglePreview> const& rectangles,
    Settings const& settings,
    PrimitiveFieldInserter inserter) {
  if (!document || !document->isActive()) {
    return {.placed = false, .error = "An active document is required."};
  }
  if (undoableActionInProgress()) {
    return {.placed = false,
            .error = "Finish the current editor action before placement."};
  }

  try {
    validatePreview(layout, rectangles);
    auto world = document->getWorld();
    auto const existingCount = world->getNumPrimitives();
    if (rectangles.size() >
        static_cast<size_t>(BW_WORLD_PRIMITIVE_COUNT_MAX - existingCount)) {
      return {.placed = false,
              .error = "The Rectangle field exceeds remaining world capacity."};
    }

    // Construction and validation intentionally finish before the document or
    // undo history is touched.
    auto batch = buildBatch(rectangles);
    if (!inserter) {
      inserter = [](bw::core::World& target, bw::core::Primitive* primitive) {
        return target.addPrimitive(primitive);
      };
    }

    auto placed = transactUndoableActionAtomically(
        document,
        std::format("Place {} Rectangle Primitive(s)", batch.size()),
        [&](Document* doc) {
          auto target = doc->getWorld();
          auto firstIndex = target->getNumPrimitives();
          std::set<uint32_t> generatedIndices;

          for (auto& primitive : batch) {
            auto before = target->getNumPrimitives();
            auto raw = primitive.get();
            uint32_t index = ~0u;
            try {
              index = inserter(*target, raw);
            } catch (...) {
              // World owns the pointer if an inserter appended before failing.
              if (target->getNumPrimitives() > before &&
                  target->getPrimitive(before) == raw) {
                primitive.release();
              }
              throw;
            }
            if (target->getNumPrimitives() != before + 1 || index != before ||
                target->getPrimitive(index) != raw) {
              if (target->getNumPrimitives() > before &&
                  target->getPrimitive(before) == raw) {
                primitive.release();
              }
              throw std::runtime_error(
                  "Rectangle insertion did not append exactly one primitive.");
            }
            primitive.release();
            generatedIndices.insert(index);
          }

          if (generatedIndices.size() != rectangles.size() ||
              (generatedIndices.empty() ? firstIndex != target->getNumPrimitives()
                                        : *generatedIndices.begin() != firstIndex)) {
            throw std::runtime_error(
                "Rectangle insertion produced an invalid generated selection.");
          }
          doc->setSelectedPrimitiveIndices(generatedIndices);
          return true;
        });
    if (!placed) {
      return {.placed = false,
              .error = "Rectangle field placement did not modify the document."};
    }

    generateClipping(document, settings, ED_CLIP_ON_PRIM_CREATE_DELETE);
    return {.placed = true, .error = {}};
  } catch (std::exception const& error) {
    return {.placed = false,
            .error = std::string("Rectangle field placement failed: ") +
                     error.what()};
  }
}

}  // namespace editor
