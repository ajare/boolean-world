#include "PrimitiveFieldPlacement.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <core/CirclePolygon.h>
#include <core/Defines.h>
#include <core/Primitive.h>
#include <core/RectanglePolygon.h>
#include <core/RegularPolygon.h>
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

float cross(wp::Vector2 const& lhs, wp::Vector2 const& rhs) {
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

bool contourContains(
    std::vector<wp::Vector2> const& contour,
    wp::Vector2 const& point,
    float tolerance) {
  float twiceArea = 0.0f;
  for (size_t i = 0; i < contour.size(); ++i) {
    twiceArea += cross(contour[i], contour[(i + 1) % contour.size()]);
  }
  auto orientation = twiceArea >= 0.0f ? 1.0f : -1.0f;
  for (size_t i = 0; i < contour.size(); ++i) {
    auto edge = contour[(i + 1) % contour.size()] - contour[i];
    auto relative = point - contour[i];
    auto edgeTolerance = tolerance * std::max(1.0f, edge.length());
    if (orientation * cross(edge, relative) < -edgeTolerance) {
      return false;
    }
  }
  return true;
}

void validatePreview(
    bw::core::PrimitiveFieldLayout const& layout,
    std::vector<PrimitiveFieldPrimitivePreview> const& primitives) {
  if (layout.sites.empty() || layout.sites.size() != layout.cells.size() ||
      primitives.empty()) {
    throw std::runtime_error(
        "Placement requires a complete layout with at least one occupied cell.");
  }

  auto const tolerance = bw::core::PrimitiveFieldNumericTolerance;
  std::map<size_t, float> cellPrimitiveSizes;
  std::map<std::pair<float, float>, size_t> siteOwners;
  std::set<size_t> holeCellIndices;
  for (auto const& primitive : primitives) {
    if (primitive.cellIndex >= layout.cells.size()) {
      throw std::runtime_error(
          "A primitive preview has an invalid cell index.");
    }
    if (primitive.isHole) {
      auto cellPrimitive = cellPrimitiveSizes.find(primitive.cellIndex);
      if (layout.sites[primitive.cellIndex] == wp::Vector2::ZERO ||
          cellPrimitive == cellPrimitiveSizes.end() ||
          !holeCellIndices.insert(primitive.cellIndex).second ||
          (primitive.regularSideCount != 3 &&
           primitive.regularSideCount != 4 &&
           primitive.regularSideCount != 6) ||
          primitive.size != cellPrimitive->second * 0.5f) {
        throw std::runtime_error(
            "A hole preview must follow one occupied cell primitive and use half its size.");
      }
    } else if (!cellPrimitiveSizes
                    .emplace(primitive.cellIndex, primitive.size)
                    .second) {
      throw std::runtime_error(
          "A cell has more than one non-hole primitive preview.");
    }

    auto const& site = layout.sites[primitive.cellIndex];
    auto [siteOwner, insertedSite] =
        siteOwners.emplace(std::pair{site.x, site.y}, primitive.cellIndex);
    if ((!insertedSite && siteOwner->second != primitive.cellIndex) ||
        !finitePoint(site)) {
      throw std::runtime_error(
          "Primitive previews reference duplicate or non-finite sites.");
    }
    auto const& cell = layout.cells[primitive.cellIndex];
    if (cell.vertices.size() < 3 ||
        primitive.position != site || !std::isfinite(primitive.size) ||
        primitive.size <= 0.0f || !std::isfinite(primitive.angle) ||
        primitive.angle < 0.0f || primitive.angle >= 360.0f ||
        primitive.contour.size() < 3 ||
        (primitive.isHole &&
         primitive.contour.size() != primitive.regularSideCount) ||
        !std::all_of(
            primitive.contour.begin(), primitive.contour.end(), finitePoint)) {
      throw std::runtime_error("A primitive preview is incomplete or invalid.");
    }

    float cellTwiceArea = 0.0f;
    for (size_t vertexIndex = 0; vertexIndex < cell.vertices.size();
         ++vertexIndex) {
      auto const& previous = cell.vertices[(vertexIndex + cell.vertices.size() - 1) % cell.vertices.size()];
      auto const& vertex = cell.vertices[vertexIndex];
      auto const& next =
          cell.vertices[(vertexIndex + 1) % cell.vertices.size()];
      if (!finitePoint(vertex) || vertex == next) {
        throw std::runtime_error(
            "A Voronoi cell contains an invalid or duplicate vertex.");
      }
      cellTwiceArea += cross(vertex, next);
      if (cross(vertex - previous, next - vertex) <= 0.0f) {
        throw std::runtime_error(
            "A Voronoi cell is degenerate or not strictly convex.");
      }
      if (!primitive.isHole &&
          !contourContains(primitive.contour, vertex, tolerance)) {
        throw std::runtime_error(
            "A fitted primitive does not contain its complete Voronoi cell.");
      }
    }
    if (!std::isfinite(cellTwiceArea) || cellTwiceArea <= tolerance ||
        !contourContains(cell.vertices, site, tolerance)) {
      throw std::runtime_error(
          "A Voronoi cell is malformed or does not contain its retained site.");
    }
  }
}

std::unique_ptr<bw::core::Primitive> createPrimitive(
    PrimitiveFieldPrimitivePreview const& preview) {
  using Primitive = bw::core::Primitive;
  if (preview.isHole) {
    return std::make_unique<bw::core::RegularPolygon>(
        Primitive::Operation::Difference, Primitive::FillRule::NonZero,
        preview.regularSideCount);
  }
  switch (preview.type) {
    case PrimitiveFieldType::Rectangle:
      return std::make_unique<bw::core::RectanglePolygon>(
          Primitive::Operation::Union, Primitive::FillRule::NonZero,
          PrimitiveFieldRectangleXyRatio);
    case PrimitiveFieldType::Triangle:
      return std::make_unique<bw::core::RegularPolygon>(
          Primitive::Operation::Union, Primitive::FillRule::NonZero, 3);
    case PrimitiveFieldType::Pentagon:
      return std::make_unique<bw::core::RegularPolygon>(
          Primitive::Operation::Union, Primitive::FillRule::NonZero, 5);
    case PrimitiveFieldType::Hexagon:
      return std::make_unique<bw::core::RegularPolygon>(
          Primitive::Operation::Union, Primitive::FillRule::NonZero, 6);
    case PrimitiveFieldType::Circle:
      return std::make_unique<bw::core::CirclePolygon>(
          Primitive::Operation::Union, Primitive::FillRule::NonZero,
          PrimitiveFieldCircleResolution);
  }
  throw std::runtime_error("The primitive preview has an unsupported type.");
}

std::vector<std::unique_ptr<bw::core::Primitive>> buildBatch(
    std::vector<PrimitiveFieldPrimitivePreview> const& previews) {
  std::vector<std::unique_ptr<bw::core::Primitive>> batch;
  batch.reserve(previews.size());
  for (auto const& preview : previews) {
    auto primitive = createPrimitive(preview);
    _setPrimitiveParameters(
        primitive.get(), 0, 0, preview.position, wp::Vector2::ZERO,
        preview.size, preview.angle);
    setPrimitiveDefaultMaterials(primitive.get());

    auto const& angle = primitive->getAnimationInterpolator(
        bw::core::VertexTransformer::Key::Angle);
    auto const& points = angle.getPoints();
    auto expectedOperation =
        preview.isHole ? bw::core::Primitive::Operation::Difference
                       : bw::core::Primitive::Operation::Union;
    if (primitive->getOperation() != expectedOperation ||
        primitive->getFillRule() != bw::core::Primitive::FillRule::NonZero ||
        primitive->getLayer() != 0 || primitive->getPriority() != 0 ||
        primitive->getOrientation() != 0.0f ||
        primitive->getPosition() != preview.position ||
        primitive->getSize() != wp::Vector2{preview.size, preview.size} ||
        points.size() != 2 || points[0] != std::pair{0.0f, preview.angle} ||
        points[1] != std::pair{1.0f, preview.angle} ||
        !primitive->isStatic()) {
      throw std::runtime_error(
          "A primitive could not be initialized with editor defaults.");
    }

    primitive->calculateAnimationValues();
    primitive->updateVertexPositions();
    auto const& transformed = primitive->getVertices();
    if (transformed.size() != 1 || transformed[0].size() != 1 ||
        transformed[0][0].size() != preview.contour.size()) {
      throw std::runtime_error(
          "A placed primitive contour does not match its preview type.");
    }
    for (size_t i = 0; i < preview.contour.size(); ++i) {
      auto delta = transformed[0][0][i].p - preview.contour[i];
      if (std::abs(delta.x) > bw::core::PrimitiveFieldNumericTolerance ||
          std::abs(delta.y) > bw::core::PrimitiveFieldNumericTolerance) {
        throw std::runtime_error(std::format(
            "A placed primitive contour does not match its preview geometry "
            "(type {}, vertex {}, preview {}, {}, actual {}, {}, delta {}, {}).",
            static_cast<int>(preview.type), i, preview.contour[i].x,
            preview.contour[i].y, transformed[0][0][i].p.x,
            transformed[0][0][i].p.y, delta.x, delta.y));
      }
    }
    batch.push_back(std::move(primitive));
  }
  return batch;
}

}  // namespace

PrimitiveFieldPlacementResult placePrimitiveField(
    Document* document,
    bw::core::PrimitiveFieldLayout const& layout,
    std::vector<PrimitiveFieldPrimitivePreview> const& primitives,
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
    validatePreview(layout, primitives);
    auto world = document->getWorld();
    auto const existingCount = world->getNumPrimitives();
    if (primitives.size() >
        static_cast<size_t>(BW_WORLD_PRIMITIVE_COUNT_MAX - existingCount)) {
      return {.placed = false,
              .error = "The primitive field exceeds remaining world capacity."};
    }

    // Construction and validation intentionally finish before the document or
    // undo history is touched.
    auto batch = buildBatch(primitives);
    if (!inserter) {
      inserter = [](bw::core::World& target, bw::core::Primitive* primitive) {
        return target.addPrimitive(primitive);
      };
    }

    auto placed = transactUndoableActionAtomically(
        document,
        std::format("Place {} Field Primitive(s)", batch.size()),
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
                  "Field insertion did not append exactly one primitive.");
            }
            primitive.release();
            generatedIndices.insert(index);
          }

          if (generatedIndices.size() != primitives.size() ||
              (generatedIndices.empty() ? firstIndex != target->getNumPrimitives()
                                        : *generatedIndices.begin() != firstIndex)) {
            throw std::runtime_error(
                "Field insertion produced an invalid generated selection.");
          }
          doc->setSelectedPrimitiveIndices(generatedIndices);
          return true;
        });
    if (!placed) {
      return {.placed = false,
              .error = "Primitive field placement did not modify the document."};
    }

    generateClipping(document, settings, ED_CLIP_ON_PRIM_CREATE_DELETE);
    return {.placed = true, .error = {}};
  } catch (std::exception const& error) {
    return {.placed = false,
            .error = std::string("Primitive field placement failed: ") +
                     error.what()};
  }
}

}  // namespace editor
