#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/DefinePrefabs.h>
#include <core/LayerBuildStep.h>
#include <core/MeshPrimitive.h>
#include <core/PrefabField.h>
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

class RefusingStep final : public bw::core::LayerBuildStep {
  bw::core::Primitive* mPrimitive;

public:
  explicit RefusingStep(bw::core::Primitive* primitive)
      : mPrimitive(primitive) {
  }

  ~RefusingStep() override {
    delete mPrimitive;
  }

  std::string getType() const override {
    return "RefusingStep";
  }

  bool mayBeFirstStep() const override {
    return false;
  }

  bw::core::LayerBuildStep* copy(
      std::map<bw::core::VertexTransformerObject const*, bw::core::VertexTransformerObject*>& primitiveMap) const override {
    auto* primitive = mPrimitive->copy();
    primitiveMap[mPrimitive] = primitive;
    return new RefusingStep(primitive);
  }

  void execute(bw::core::LayerBuildContext& context) const override {
    context.appendPrimitive(mPrimitive);
  }

  bool primitivesParticipateInBuild() const override {
    return true;
  }

  bool permitsDirectPrimitiveEditing() const override {
    return false;
  }

  bool acceptsNewPrimitives() const override {
    return false;
  }

  uint32_t adoptPrimitive(bw::core::Primitive* primitive) override {
    if (mPrimitive) {
      throw std::runtime_error("RefusingStep already owns a Primitive");
    }
    mPrimitive = primitive;
    return 0;
  }

  void replacePrimitive(
      bw::core::Primitive* oldPrimitive,
      bw::core::Primitive* newPrimitive) override {
    if (oldPrimitive != mPrimitive) {
      throw std::runtime_error("Primitive not owned by RefusingStep");
    }
    delete mPrimitive;
    mPrimitive = newPrimitive;
  }

  bool ownsPrimitive(bw::core::Primitive const* primitive) const override {
    return mPrimitive == primitive;
  }

private:
  void serializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) const override {
  }

  bool deserializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) override {
    return true;
  }
};

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

void theGhostIsHiddenFromTheViewAndTheFoldInMeshMode() {
  editor::Document document;
  editor::Settings settings;

  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto const* ghost = document.getGhost();

  require(editor::primitiveVisibleForActiveStep(*layer, ghost, settings),
          "the ghost was hidden in Primitive mode");

  settings.mode = editor::Settings::Mode::Mesh;
  require(!editor::primitiveVisibleForActiveStep(*layer, ghost, settings),
          "the ghost was still shown - and still folded - in Mesh mode");
  require(document.meshIneligibilityReason(ED_GHOST_INDEX).find("ghost") != std::string::npos,
          "the ghost was eligible to become the active mesh");
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

void prefabPrimitivesAreVisibleAndFoldedInIsolationOnlyWhileTheirPrefabIsSelected() {
  editor::Document document;
  editor::Settings settings;
  settings.showAllStepPrimitives = true;

  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  layer->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));
  auto* earlierStepPrimitive = layer->getPrimitive(layer->getNumPrimitives() - 1);
  auto* step = new bw::core::DefinePrefabs();
  auto stepIndex = layer->addStep(step);
  auto* prefab = step->addPrefab("Visible");
  layer->setActiveStep(stepIndex);
  require(!editor::primitiveVisibleForActiveStep(
              *layer, document.getGhost(), settings),
          "an unselected DefinePrefabs step still showed the authoring ghost");
  settings.mode = editor::Settings::Mode::Mesh;
  require(document.meshDrawToolUnavailableReason(settings) ==
              "Select a Prefab first.",
          "the Mesh draw tool did not explain that a Prefab must be selected");
  settings.mode = editor::Settings::Mode::Primitive;

  step->setSelectedPrefab(prefab);
  layer->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));
  auto* primitive = layer->getPrimitive(layer->getNumPrimitives() - 1);

  require(editor::primitiveVisibleForActiveStep(*layer, primitive, settings),
          "the selected Prefab was hidden while its DefinePrefabs step was active");
  require(editor::primitiveParticipatesInEditorFold(*layer, primitive, settings),
          "a selected Prefab's own Primitive was withheld from the editor fold while it was active");
  require(!editor::primitiveParticipatesInEditorFold(*layer, earlierStepPrimitive, settings),
          "an earlier step's Primitive was folded alongside an active Prefab, "
          "which should clip in isolation");

  layer->setActiveStep(0);
  require(!editor::primitiveVisibleForActiveStep(*layer, primitive, settings),
          "showAllStepPrimitives exposed a Prefab while another step was active");
  require(!editor::primitiveParticipatesInEditorFold(*layer, primitive, settings),
          "a Prefab Primitive was folded while another step was active");
}

void theGhostIsHiddenWhileAPrefabFieldStepIsActive() {
  editor::Document document;
  editor::Settings settings;

  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs();
  layer->addStep(definitions);
  auto* field = new bw::core::PrefabField();
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);

  layer->setActiveStep(fieldIndex);
  require(!editor::primitiveVisibleForActiveStep(*layer, document.getGhost(), settings),
          "the authoring ghost was shown while a PrefabField step was active, "
          "but PrefabField never accepts a new Primitive");
}

void refusingStepPrimitivesAreNotSelectableInPrimitiveMode() {
  editor::Document document;
  editor::Settings settings;
  settings.showAllStepPrimitives = true;

  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  primitive->setPosition({100.0f, 100.0f});
  auto refusingIndex = layer->addStep(new RefusingStep(primitive));
  layer->setActiveStep(refusingIndex);

  require(document.getHoveredPrimitiveIndices({100.0f, 100.0f}, settings).empty(),
          "a Primitive from a refusing step responded to hover selection");
  require(document.getPrimitiveIndicesInBounds(
                      wp::BoundingBox({90.0f, 90.0f}, {20.0f, 20.0f}), settings)
              .empty(),
          "a Primitive from a refusing step responded to box selection");
  auto selectable = document.getSelectablePrimitiveIndices(settings);
  require(std::find(selectable.begin(), selectable.end(), primitive->getId()) == selectable.end(),
          "Select All included a Primitive from a refusing step");
}

void meshEligibilityRequiresTheSelectedDirectlyEditableStep() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();

  auto* editableMesh = new bw::core::MeshPrimitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      {{{{{-1.0f, -1.0f}}, {{1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{-1.0f, 1.0f}}}}});
  document.getWorld()->addPrimitive(editableMesh);
  auto editableIndex = editableMesh->getId();

  auto* refusedMesh = new bw::core::MeshPrimitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      {{{{{9.0f, 9.0f}}, {{11.0f, 9.0f}}, {{11.0f, 11.0f}}, {{9.0f, 11.0f}}}}});
  auto refusingStepIndex = layer->addStep(new RefusingStep(refusedMesh));

  layer->setActiveStep(refusingStepIndex);
  require(document.meshIneligibilityReason(editableIndex).find("another LayerBuildStep") != std::string::npos,
          "a MeshPrimitive in another step was eligible");
  require(document.meshIneligibilityReason(refusedMesh->getId()).find("does not permit") != std::string::npos,
          "a MeshPrimitive from a refusing step was eligible");

  layer->setActiveStep(0);
  require(document.meshIneligibilityReason(editableIndex).empty() &&
              document.activateMesh(editableIndex) && document.getActiveMesh(),
          "an editable MeshPrimitive in the selected step was not eligible");
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
    theGhostIsHiddenFromTheViewAndTheFoldInMeshMode();
    primitiveIndicesInBoundsFindsOverlappingPrimitivesAndIgnoresTheGhost();
    selectionQueriesExcludePrimitivesFromLaterLayerBuildSteps();
    prefabPrimitivesAreVisibleAndFoldedInIsolationOnlyWhileTheirPrefabIsSelected();
    theGhostIsHiddenWhileAPrefabFieldStepIsActive();
    refusingStepPrimitivesAreNotSelectableInPrimitiveMode();
    meshEligibilityRequiresTheSelectedDirectlyEditableStep();
    openingADocumentReplacesTheActiveDocument();
    std::cout << "Document selection and hover queries passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
