#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/RectanglePolygon.h>

#include "Defines.h"
#include "Document.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void changingSelectedPrimitiveIndicesDoesNotWriteIntoAnInputRange() {
  editor::Document document;
  document.setSelectedPrimitiveIndices({2, 4});

  document.addSelectedPrimitiveIndices({1, 4, 8});
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>({1, 2, 4, 8}),
          "adding selected primitive indices did not preserve their union");

  document.removeSelectedPrimitiveIndices({2, 7, 8});
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>({1, 4}),
          "removing selected primitive indices did not preserve their difference");
}

void primitiveHoverQueriesAreSafeWithoutAnActiveDocument() {
  editor::Document document;
  editor::Settings settings;
  settings.renderAnimatedPrimitives = false;

  require(document.getHoveredPrimitiveIndex({}, settings) == ~0u,
          "primitive hover query did not report no primitive without an active document");
  require(document.getHoveredPrimitiveIndices({}, settings).empty(),
          "primitive hover query did not report no primitives without an active document");
}

void theGhostIsHoveredFirstWhereItOverlapsAnotherPrimitive() {
  editor::Document document;
  editor::Settings settings;

  document.newDoc();

  // A new document seeds its ghost at the origin, and this lands on top of
  // it, so the cursor there is over both.
  document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f));

  auto const hovered = document.getHoveredPrimitiveIndices({0.0f, 0.0f}, settings);

  require(hovered.size() > 1,
          "the test did not put more than one primitive under the cursor");
  require(hovered.front() == uint32_t(ED_GHOST_INDEX),
          "the ghost did not come first among the hovered primitives");
  require(document.getHoveredPrimitiveIndex({0.0f, 0.0f}, settings) == uint32_t(ED_GHOST_INDEX),
          "the hovered primitive was not the ghost where it overlaps another primitive");
}

void openingADocumentReplacesTheActiveDocument() {
  auto const filepath = std::filesystem::temp_directory_path() / "boolean-world-document-open-test.yaml";

  editor::Document document;
  document.newDoc();
  document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f));
  document.saveDocAs(filepath.string());
  document.newDoc();

  require(document.openDoc(filepath.string()),
          "opening a document did not replace the active document");
  require(document.isActive(), "opening a document left it inactive");

  std::filesystem::remove(filepath);
}

}  // namespace

int main() {
  try {
    changingSelectedPrimitiveIndicesDoesNotWriteIntoAnInputRange();
    primitiveHoverQueriesAreSafeWithoutAnActiveDocument();
    theGhostIsHoveredFirstWhereItOverlapsAnotherPrimitive();
    openingADocumentReplacesTheActiveDocument();
    std::cout << "Document selection and hover queries passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
