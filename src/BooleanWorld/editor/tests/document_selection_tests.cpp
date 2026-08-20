#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/PrimitiveField.h>
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

void primitiveIndicesInBoundsFindsOverlappingPrimitivesAndIgnoresTheGhost() {
  editor::Document document;
  editor::Settings settings;

  document.newDoc();

  // Lands well clear of the origin (and so of the ghost seeded there), so a
  // bounds query can distinguish "found this primitive" from "found the
  // ghost too".
  auto primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  primitive->setPosition({100.0f, 100.0f});
  document.getWorld()->addPrimitive(primitive);

  auto overlapping = document.getPrimitiveIndicesInBounds(
      wp::BoundingBox({90.0f, 90.0f}, {20.0f, 20.0f}), settings);
  require(overlapping.size() == 1 && overlapping.front() != uint32_t(ED_GHOST_INDEX),
          "a bounds query missed a primitive its rectangle overlaps");

  auto elsewhere = document.getPrimitiveIndicesInBounds(
      wp::BoundingBox({-500.0f, -500.0f}, {20.0f, 20.0f}), settings);
  require(elsewhere.empty(),
          "a bounds query found a primitive outside its rectangle");
}

void selectionQueriesExcludePrimitivesFromLaterLayerBuildSteps() {
  editor::Document document;
  editor::Settings settings;
  settings.showAllStepPrimitives = false;

  document.newDoc();

  auto* layer = document.getWorld()->getActiveLayer();

  // Step 0 (always present) is active by construction: this primitive lands
  // in it.
  auto* earlyStepPrimitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  earlyStepPrimitive->setPosition({100.0f, 100.0f});
  document.getWorld()->addPrimitive(earlyStepPrimitive);

  auto laterStepIndex = layer->addStep(new bw::core::PrimitiveField());
  layer->setActiveStep(laterStepIndex);

  auto* laterStepPrimitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  laterStepPrimitive->setPosition({100.0f, 100.0f});
  document.getWorld()->addPrimitive(laterStepPrimitive);

  // Back to step 0 as the authoring/selection context: the later step's
  // primitive is out of context and should drop out of every selection
  // query, even though it geometrically coincides with the one that stays.
  layer->setActiveStep(0);

  auto selectable = document.getSelectablePrimitiveIndices(settings);
  require(std::find(selectable.begin(), selectable.end(), earlyStepPrimitive->getId()) != selectable.end(),
          "Select All dropped a Primitive that belongs to the active step");
  require(std::find(selectable.begin(), selectable.end(), laterStepPrimitive->getId()) == selectable.end(),
          "Select All picked up a Primitive from a step later than the active one");

  auto inBounds = document.getPrimitiveIndicesInBounds(
      wp::BoundingBox({90.0f, 90.0f}, {20.0f, 20.0f}), settings);
  require(std::find(inBounds.begin(), inBounds.end(), laterStepPrimitive->getId()) == inBounds.end(),
          "a bounds query picked up a Primitive from a step later than the active one");

  // Opting back into every step restores it.
  settings.showAllStepPrimitives = true;
  auto selectableAllSteps = document.getSelectablePrimitiveIndices(settings);
  require(std::find(selectableAllSteps.begin(), selectableAllSteps.end(), laterStepPrimitive->getId()) != selectableAllSteps.end(),
          "showAllStepPrimitives did not restore a later step's Primitive to Select All");
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
    primitiveIndicesInBoundsFindsOverlappingPrimitivesAndIgnoresTheGhost();
    selectionQueriesExcludePrimitivesFromLaterLayerBuildSteps();
    openingADocumentReplacesTheActiveDocument();
    std::cout << "Document selection and hover queries passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
