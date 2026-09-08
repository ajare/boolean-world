#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/DefinePrefabs.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/LayerBuildStep.h>
#include <core/MeshPrimitive.h>
#include <core/PrefabField.h>
#include <core/PrimitiveField.h>
#include <core/RectanglePolygon.h>
#include <core-lua/CoreLua.h>
#include <core-lua/RunScript.h>
#include <core-lua/ScriptRuntime.h>

#include "Defines.h"
#include "Document.h"
#include "Selection.h"
#include "Settings.h"
#include "UiHelpers.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();
editor::Settings gEditorSettings;

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}

void regenerateWorldData(Document*) {
}
}  // namespace editor

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

  void releasePrimitive(bw::core::Primitive* primitive) override {
    if (primitive != mPrimitive) {
      throw std::runtime_error("Primitive not owned by RefusingStep");
    }
    mPrimitive = nullptr;
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

class SelectionWithoutWorld final : public editor::Selection {
  bw::core::World const* selectionWorld() const override { return nullptr; }
};

void changingSelectedPrimitiveIndicesDoesNotWriteIntoAnInputRange() {
  SelectionWithoutWorld selection;
  selection.setSelectedPrimitiveIndices({2, 4});

  selection.addSelectedPrimitiveIndices({1, 4, 8});
  require(selection.getSelectedPrimitiveIndices() == std::set<uint32_t>({1, 2, 4, 8}),
          "adding selected primitive indices did not preserve their union");

  selection.removeSelectedPrimitiveIndices({2, 7, 8});
  require(selection.getSelectedPrimitiveIndices() == std::set<uint32_t>({1, 4}),
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

void selectAllAndABoundsQueryOverTheOriginExcludeTheGhostEvenWhileItIsActive() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = true;

  document.newDoc();

  // Lands on top of the ghost, which a new document seeds at the origin.
  auto* primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  document.getWorld()->addPrimitive(primitive);

  auto selectable = document.getSelectablePrimitiveIndices(settings);
  require(std::find(selectable.begin(), selectable.end(), uint32_t(ED_GHOST_INDEX)) == selectable.end(),
          "Select All picked up the ghost while it was active");
  require(std::find(selectable.begin(), selectable.end(), primitive->getId()) != selectable.end(),
          "Select All dropped a real Primitive that overlaps the ghost");

  auto overOrigin = document.getPrimitiveIndicesInBounds(
      wp::BoundingBox({0.0f, 0.0f}, {20.0f, 20.0f}), settings);
  require(std::find(overOrigin.begin(), overOrigin.end(), uint32_t(ED_GHOST_INDEX)) == overOrigin.end(),
          "a rubber-band over the origin picked up the ghost while it was active");
  require(std::find(overOrigin.begin(), overOrigin.end(), primitive->getId()) != overOrigin.end(),
          "a rubber-band over the origin dropped a real Primitive that overlaps the ghost");
}

void selectionQueriesIncludeOnlyTheActiveLayerBuildStep() {
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

  // While the later step is active, the earlier Primitive is out of context
  // even though it geometrically coincides with the one that stays.
  auto laterSelectable = document.getSelectablePrimitiveIndices(settings);
  require(std::find(laterSelectable.begin(), laterSelectable.end(), laterStepPrimitive->getId()) != laterSelectable.end(),
          "Select All dropped a Primitive that belongs to the active later step");
  require(std::find(laterSelectable.begin(), laterSelectable.end(), earlyStepPrimitive->getId()) == laterSelectable.end(),
          "Select All picked up a Primitive from a step earlier than the active one");
  require(!editor::primitiveVisibleForActiveStep(*layer, earlyStepPrimitive, settings),
          "the world view showed a Primitive from a step earlier than the active one");

  // Back to step 0 as the authoring/selection context: now the later step's
  // Primitive is out of context and should drop out of every selection query.
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
  require(editor::primitiveVisibleForActiveStep(
              *layer, document.getGhost(), settings),
          "the authoring ghost was hidden while editing a selected Prefab");
  require(!editor::primitiveVisibleForActiveStep(
              *layer, earlierStepPrimitive, settings),
          "show-all rendered another Primitive while editing a selected Prefab");
  require(editor::primitiveParticipatesInEditorFold(*layer, primitive, settings),
          "a selected Prefab's own Primitive was withheld from the editor fold while it was active");
  require(!editor::primitiveParticipatesInEditorFold(*layer, earlierStepPrimitive, settings),
          "an earlier step's Primitive was folded alongside an active Prefab, "
          "which should clip in isolation");
  auto prefabScope = editor::inScopePrimitives(
      *document.getWorld(), bw::core::SelectLayer(layer->getId()), settings);
  require(prefabScope.size() == 1 && prefabScope.front() == primitive,
          "the in-scope Primitive list did not isolate the selected Prefab");

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

void activePrefabFieldPrimitivesUseTheActiveStepColour() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs();
  auto definitionsIndex = layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("Visible instance");
  definitions->setSelectedPrefab(prefab);
  layer->setActiveStep(definitionsIndex);
  document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f));

  auto* field = new bw::core::PrefabField();
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  require(field->placeSelected(
              *layer, {bw::core::PrefabTileSize::Size64, 0, 0}),
          "the PrefabField colour fixture did not place its instance");
  definitions->clearSelectedPrefab();
  layer->setActiveStep(fieldIndex);
  layer->rebuild();

  auto* output = layer->getPrimitive(layer->getNumPrimitives() - 1);
  require(layer->getOwningStepIndex(output) == fieldIndex,
          "the PrefabField colour fixture did not produce its Primitive");
  require(!editor::primitiveFadedForActiveStep(*layer, output),
          "an active PrefabField Primitive used the inactive-step colour");

  layer->setActiveStep(0);
  require(editor::primitiveFadedForActiveStep(*layer, output),
          "an inactive PrefabField Primitive retained the active-step colour");
}

void activeRunScriptPrimitivesAreVisibleAndUseTheActiveStepColour() {
  bw::core::ScriptRuntime runtime;
  runtime.load("visible-output", R"(
    local room = context:create_primitive("Rectangle")
    room:set_size(64, 32)
    room:set_position(128, 64)
    room:set_operation("union")
    room:set_priority(0)
    context:place_primitive(room)
  )");

  editor::Document document;
  editor::Settings settings;
  document.setPrimitiveFilter(
      [&settings](bw::core::Layer const& candidateLayer,
                  bw::core::Primitive const* primitive) {
        return editor::primitiveParticipatesInEditorFold(
            candidateLayer, primitive, settings);
      });
  document.newDoc();
  auto world = document.getWorld();
  auto* layer = world->getActiveLayer();
  auto* runScript = new bw::core::RunScript(runtime);
  runScript->setScriptName("visible-output");
  auto runScriptIndex = layer->addStep(runScript);
  layer->setActiveStep(runScriptIndex);

  auto* output = layer->getPrimitive(layer->getNumPrimitives() - 1);
  require(layer->getOwningStepIndex(output) == runScriptIndex,
          "the RunScript visibility fixture did not produce its Primitive");
  require(editor::primitiveVisibleForActiveStep(*layer, output, settings),
          "an active RunScript Primitive was hidden by the editor step filter");
  require(!editor::primitiveFadedForActiveStep(*layer, output),
          "an active RunScript Primitive used the inactive-step colour");
  require(!output->getVertices().empty(),
          "an active RunScript Primitive had no transformed vertices");

  auto visible = world->findPrimitives(world->getExtents());
  require(std::find(visible.begin(), visible.end(), output) != visible.end(),
          "an active RunScript Primitive was missing from the render lookup grid");

  auto* generator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
      world->getWorldDataGenerator());
  require(generator, "the editor World had no DynamicWorldDataGenerator");
  generator->generateBlocking();
  auto worldData = generator->getWorldData(world.get());
  auto clippingPrimitives = generator->getActiveClippingPrimitives();
  require(clippingPrimitives.size() == 1,
          "the editor fold did not admit exactly the active RunScript Primitive");
  require(worldData && !worldData->getTriangles().empty(),
          "an active RunScript Primitive produced no rendered Arrangement geometry");

  layer->setActiveStep(0);
  require(editor::primitiveFadedForActiveStep(*layer, output),
          "an inactive RunScript Primitive retained the active-step colour");
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

  auto* editableMesh = bw::core::MeshPrimitive::fromComplexPolygons(
      bw::core::Primitive::Operation::Union,
      {{{{{-1.0f, -1.0f}}, {{1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{-1.0f, 1.0f}}}}});
  document.getWorld()->addPrimitive(editableMesh);
  auto editableIndex = editableMesh->getId();

  auto* refusedMesh = bw::core::MeshPrimitive::fromComplexPolygons(
      bw::core::Primitive::Operation::Union,
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

void inScopePrimitivesAndGroundingResolutionFollowFoldOrder() {
  editor::Document document;
  editor::Settings settings;
  document.newDoc();

  auto* world = document.getWorld().get();
  auto* firstLayer = world->getActiveLayer();
  auto makePrimitive = [&](float floorZ, uint8_t priority) {
    auto* primitive = new bw::core::RectanglePolygon(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero, 1.0f);
    primitive->setPosition({100.0f, 100.0f});
    primitive->setPriority(priority);
    auto properties = primitive->getProperties();
    properties.floorZ = floorZ;
    primitive->setProperties(properties);
    world->addPrimitive(primitive);
    return primitive;
  };

  auto* single = makePrimitive(12.0f, 1);
  require(editor::resolveGroundingFloorZ({single}, {100.0f, 100.0f}) ==
              std::optional<float>{12.0f},
          "grounding did not return a single containing Primitive's floor");
  auto slopedProperties = single->getProperties();
  slopedProperties.floorZ.gradient = {0.5f, 0.0f};
  single->setProperties(slopedProperties);
  require(editor::resolveGroundingFloorZ({single}, {101.0f, 100.0f}) ==
              std::optional<float>{12.5f},
          "grounding treated a translated sloped Primitive as one scalar floor");

  auto* lowerPriority = makePrimitive(24.0f, 2);
  auto* higherPriority = makePrimitive(36.0f, 3);
  require(editor::resolveGroundingFloorZ(
              {lowerPriority, higherPriority}, {100.0f, 100.0f}) ==
              std::optional<float>{36.0f},
          "grounding did not choose the highest-priority containing Primitive");

  auto* equalPriorityEarlier = makePrimitive(48.0f, 4);
  auto* equalPriorityLater = makePrimitive(60.0f, 4);
  require(editor::resolveGroundingFloorZ(
              {equalPriorityEarlier, equalPriorityLater}, {100.0f, 100.0f}) ==
              std::optional<float>{60.0f},
          "grounding did not choose the later Primitive at equal priority");
  require(!editor::resolveGroundingFloorZ(
              {single, lowerPriority, higherPriority}, {300.0f, 300.0f}),
          "grounding found a Primitive outside the in-scope geometry");

  auto* secondLayer = world->addLayer("Second");
  world->setActiveLayer(secondLayer);
  auto* secondLayerPrimitive = makePrimitive(72.0f, 5);
  world->setActiveLayer(firstLayer);

  auto firstLayerPrimitives = editor::inScopePrimitives(
      *world, bw::core::SelectLayer(firstLayer->getId()), settings);
  require(std::find(firstLayerPrimitives.begin(), firstLayerPrimitives.end(),
                    single) != firstLayerPrimitives.end() &&
              std::find(firstLayerPrimitives.begin(), firstLayerPrimitives.end(),
                        secondLayerPrimitive) == firstLayerPrimitives.end(),
          "in-scope Primitives did not compose the selected Layer's fold predicate");

  auto secondLayerPrimitives = editor::inScopePrimitives(
      *world, bw::core::SelectLayer(secondLayer->getId()), settings);
  require(std::find(secondLayerPrimitives.begin(), secondLayerPrimitives.end(),
                    secondLayerPrimitive) != secondLayerPrimitives.end() &&
              std::find(secondLayerPrimitives.begin(), secondLayerPrimitives.end(),
                        single) == secondLayerPrimitives.end(),
          "in-scope Primitives included a Primitive outside layerSelection");
}

void worldYamlSerializationRequiresTheWorldYamlExtension() {
  auto const invalidPath = std::filesystem::temp_directory_path() /
                           "boolean-world-invalid-extension.yaml";

  editor::Document document;
  document.newDoc();

  bool saveThrew = false;
  try {
    document.saveDocAs(invalidPath.string());
  } catch (std::exception const&) {
    saveThrew = true;
  }
  require(saveThrew, "saving a YAML World without .world.yaml did not throw");
  require(!std::filesystem::exists(invalidPath),
          "saving a YAML World with an invalid extension wrote a file");
  require(!document.hasFilepath(),
          "a rejected YAML World save retained the invalid filepath");

  bool openThrew = false;
  try {
    document.openDoc(invalidPath.string());
  } catch (std::exception const&) {
    openThrew = true;
  }
  require(openThrew, "opening a YAML World without .world.yaml did not throw");
}

void openingAWorldWithNoPrimitivesRestoresTheEditorGhost() {
  auto const filepath = std::filesystem::temp_directory_path() /
                        "boolean-world-empty-document-open-test.world.yaml";

  editor::Document source;
  source.newDoc();
  source.saveDocAs(filepath.string());

  editor::Document loaded;
  require(loaded.openDoc(filepath.string()),
          "opening a World with no saved Primitives failed");
  require(loaded.isActive() && loaded.getWorld()->getNumPrimitives() == 1 &&
              (loaded.getGhost()->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0,
          "opening an empty World did not restore only the editor ghost");

  std::filesystem::remove(filepath);
}

void openingADocumentReplacesTheActiveDocument() {
  auto const filepath = std::filesystem::temp_directory_path() / "boolean-world-document-open-test.world.yaml";

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

void openingAWorldWhoseFirstOutputComesFromPrefabFieldRestoresTheGhost() {
  auto const filepath = std::filesystem::temp_directory_path() /
                        "boolean-world-prefab-field-document-open-test.world.yaml";

  editor::Document source;
  source.newDoc();
  auto* layer = source.getWorld()->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs();
  auto definitionsIndex = layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("Only output");
  definitions->setSelectedPrefab(prefab);
  layer->setActiveStep(definitionsIndex);
  source.getWorld()->addPrimitive(bw::core::MeshPrimitive::fromComplexPolygons(
      bw::core::Primitive::Operation::Union,
      {{{{{-1.0f, -1.0f}}, {{1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{-1.0f, 1.0f}}}}}));

  auto* field = new bw::core::PrefabField();
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  require(field->placeSelected(
              *layer, {bw::core::PrefabTileSize::Size64, 0, 0}),
          "the PrefabField open fixture did not place its instance");
  definitions->clearSelectedPrefab();
  layer->setActiveStep(fieldIndex);
  source.saveDocAs(filepath.string());

  editor::Document loaded;
  require(loaded.openDoc(filepath.string()),
          "opening a World whose first saved output came from PrefabField failed");
  require((loaded.getGhost()->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0 &&
              loaded.getGhost()->getId() == ED_GHOST_INDEX,
          "opening the PrefabField World did not restore the ghost at index 0");
  require(loaded.getWorld()->getNumPrimitives() == 3,
          "restoring the ghost lost or duplicated the PrefabField output");

  auto* loadedLayer = loaded.getWorld()->getActiveLayer();
  auto* loadedDefinitions =
      dynamic_cast<bw::core::DefinePrefabs*>(loadedLayer->getStep(definitionsIndex));
  require(loadedDefinitions && loadedDefinitions->getNumPrefabs() == 1,
          "opening the PrefabField World lost its Prefab definition");
  loadedLayer->setActiveStep(definitionsIndex);
  loadedDefinitions->setSelectedPrefab(loadedDefinitions->getPrefab(0));
  loadedLayer->rebuild();

  editor::Settings settings;
  settings.ghostActive = false;
  auto hovered = loaded.getHoveredPrimitiveIndices({0.0f, 0.0f}, settings);
  require(hovered.size() == 1 &&
              loadedLayer->getOwningStepIndex(loaded.getWorld()->getPrimitive(hovered.front())) ==
                  definitionsIndex,
          "a loaded Prefab's MeshPrimitive was not hover-selectable in Primitive mode");

  std::filesystem::remove(filepath);
}

void worldTestPrefabMeshPrimitivesAreHoverSelectable() {
  auto const filepath = std::filesystem::path(__FILE__).parent_path() /
                        "../../app/resources/world-test-1.world.yaml";

  editor::Document document;
  require(document.openDoc(filepath.lexically_normal().string()),
          "world-test-1.world.yaml did not open for its Prefab selection regression");

  auto* layer = document.getWorld()->getActiveLayer();
  auto* definitions = dynamic_cast<bw::core::DefinePrefabs*>(layer->getStep(1));
  require(definitions && definitions->getNumPrefabs() > 0,
          "world-test-1.world.yaml no longer has its expected Prefab definition");
  layer->setActiveStep(1);
  definitions->setSelectedPrefab(definitions->getPrefab(0));
  layer->rebuild();

  editor::Settings settings;
  settings.ghostActive = false;
  size_t authored = 0;
  size_t selectable = 0;
  for (uint32_t index = 0; index < document.getWorld()->getNumPrimitives();
       ++index) {
    auto* primitive = document.getWorld()->getPrimitive(index);
    if (layer->getOwningStepIndex(primitive) != 1) continue;
    ++authored;
    auto hovered = document.getHoveredPrimitiveIndices(
        primitive->getBounds().getCentre(), settings);
    selectable += std::find(hovered.begin(), hovered.end(), index) !=
                  hovered.end();
  }
  require(authored > 0 && selectable == authored,
          "world-test-1.world.yaml's Prefab MeshPrimitives were not hover-selectable in Primitive mode");
}

void worldMines3BuildsPreviewWorldData(bw::core::ScriptRuntime& runtime) {
  auto const resourceRoot = std::filesystem::path(BW_EDITOR_RESOURCE_ROOT);
  auto readText = [](std::filesystem::path const& path) {
    std::ifstream input(path);
    if (!input) {
      throw std::runtime_error("could not open " + path.string());
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
  };
  runtime.load(
      "MinesLayer", readText(resourceRoot / "scripts/mines-layer.lua"),
      {{"World/UtilityFunctions",
        readText(resourceRoot / "scripts/utility-functions.lua")}},
      {{.name = "iterations",
        .type = bw::core::ScriptParameterType::Integer,
        .defaultValue = int64_t{10},
        .integerMinimum = 1,
        .integerMaximum = 50}});

  editor::Document document;
  require(
      document.openDoc((resourceRoot / "world-mines-3.world.yaml").string()),
      "world-mines-3.world.yaml did not open");

  editor::Settings settings;
  settings.ghostActive = false;
  auto* world = document.getWorld().get();
  auto const selected = world->getWorldDataGenerator()->getLayerSelection();
  auto const inScope = editor::inScopePrimitives(*world, selected, settings);
  std::vector<bw::core::Primitive*> primitives;
  std::vector<std::uint64_t> priorities;
  for (auto const* primitive : inScope) {
    primitives.push_back(const_cast<bw::core::Primitive*>(primitive));
    priorities.push_back(primitive->getGeneratedPriority());
  }

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generateOrdered(primitives, priorities);
  auto preview = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
      world->getWedgeGenerationParameters());
  require(!preview->getTriangles().empty(),
          "world-mines-3 produced no preview triangles");
  require(preview->getHydraulicCells().size() == preview->getTriangles().size(),
          "world-mines-3 preview hydraulic cells do not match its triangles");
  require(preview->getLiquidPoolElevations().size() ==
              preview->getTriangles().size(),
          "world-mines-3 preview Pool elevations do not match its triangles");
  std::cout << "world-mines-3: " << inScope.size() << " primitives, "
            << preview->getTriangles().size() << " triangles, "
            << preview->getWalls().size() << " walls\n";
}

void aFailedOpenPreservesTheActiveDocument() {
  auto const filepath = std::filesystem::temp_directory_path() / "boolean-world-document-open-failure-test.world.yaml";

  editor::Document document;
  document.newDoc();

  auto* root = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  auto* child = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  document.getWorld()->addPrimitive(root);
  document.getWorld()->addPrimitive(child);
  child->setParent(root);

  document.saveDocAs(filepath.string());

  // Corrupt root's parentId (the first "parentId:" entry after the ghost
  // primitive's) to point at child, forming a root <-> child cycle that
  // World::deserialize rejects without throwing.
  std::ifstream in(filepath);
  std::stringstream buffer;
  buffer << in.rdbuf();
  in.close();
  auto yaml = buffer.str();

  auto const marker = std::string("parentId: ");
  auto position = yaml.find(marker);
  require(position != std::string::npos, "saved world does not contain a parentId to corrupt");
  position = yaml.find(marker, position + marker.size());
  require(position != std::string::npos, "saved world does not contain root's parentId to corrupt");
  position += marker.size();
  auto const end = yaml.find('\n', position);
  yaml.replace(position, end - position, std::to_string(child->getId()));

  std::ofstream out(filepath);
  out << yaml;
  out.close();

  document.newDoc();
  auto const previousWorld = document.getWorld();

  require(!document.openDoc(filepath.string()),
          "opening a document with a cyclic parent chain unexpectedly succeeded");
  require(document.isActive() && document.getWorld() == previousWorld,
          "a failed open replaced the active World");
  require(!document.hasFilepath(), "a failed open left a filepath set");

  std::filesystem::remove(filepath);
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    bw::core::ScriptRuntime runtime;
    bw::core::registerScriptStepTypes(runtime);

    changingSelectedPrimitiveIndicesDoesNotWriteIntoAnInputRange();
    primitiveHoverQueriesAreSafeWithoutAnActiveDocument();
    theGhostIsHoveredFirstWhereItOverlapsAnotherPrimitive();
    theGhostIsHiddenFromTheViewAndTheFoldInMeshMode();
    primitiveIndicesInBoundsFindsOverlappingPrimitivesAndIgnoresTheGhost();
    selectAllAndABoundsQueryOverTheOriginExcludeTheGhostEvenWhileItIsActive();
    selectionQueriesIncludeOnlyTheActiveLayerBuildStep();
    prefabPrimitivesAreVisibleAndFoldedInIsolationOnlyWhileTheirPrefabIsSelected();
    theGhostIsHiddenWhileAPrefabFieldStepIsActive();
    activePrefabFieldPrimitivesUseTheActiveStepColour();
    activeRunScriptPrimitivesAreVisibleAndUseTheActiveStepColour();
    refusingStepPrimitivesAreNotSelectableInPrimitiveMode();
    meshEligibilityRequiresTheSelectedDirectlyEditableStep();
    inScopePrimitivesAndGroundingResolutionFollowFoldOrder();
    worldYamlSerializationRequiresTheWorldYamlExtension();
    openingADocumentReplacesTheActiveDocument();
    openingAWorldWithNoPrimitivesRestoresTheEditorGhost();
    openingAWorldWhoseFirstOutputComesFromPrefabFieldRestoresTheGhost();
    worldTestPrefabMeshPrimitivesAreHoverSelectable();
    worldMines3BuildsPreviewWorldData(runtime);
    aFailedOpenPreservesTheActiveDocument();
    std::cout << "Document selection and hover queries passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
