#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/LayerBuildStep.h>
#include <core/RectanglePolygon.h>

#include "Actions.h"
#include "Document.h"
#include "EmbossingCatalogLibrary.h"
#include "Settings.h"
#include "Undo.h"

spdlog::logger* gLogger = nullptr;
editor::Settings gEditorSettings;

namespace editor {
void generateClipping(Document*, Settings const&, int) {}
void regenerateWorldData(Document*) {}
}  // namespace editor

namespace {
namespace fs = std::filesystem;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void writeFile(fs::path const& path, std::string const& contents) {
  std::ofstream stream(path);
  stream << contents;
  if (!stream) throw std::runtime_error("Could not write fixture " + path.string());
}

void writeFixture(fs::path const& root) {
  writeFile(root / "embossing.yaml", R"(presets:
  - id: existing
    name: Existing
    emboss:
      pattern: Square
      radius: 8.0
      depth: 0.5
      depthVariation: 0.1
      runningBondWidth: 50.0
      runningBondOffset: 50.0
      voronoiRounding: 0.25
)");
  writeFile(root / "Resources.yaml", R"(Resources:
  Resource:
    - type: "TextFile"
      name: "EmbossingFile"
      location: "embossing.yaml"
    - type: "EmbossingCatalog"
      name: "Embossing"
      DependentResources:
        DependentResource:
          id: "Yaml"
          ref: "EmbossingFile"
)");
}

void authoringPersistsAndIsUndoable(fs::path const& root) {
  auto& library = editor::embossingCatalogLibrary();
  library.load(root / "Resources.yaml");
  editor::clearUndoHistory();

  editor::Document document;
  document.newDoc();
  auto index = document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));

  bw::core::EmbossData emboss;
  emboss.pattern = bw::core::EmbossPattern::RunningBond;
  emboss.radius = 12.0f;
  std::string createdId;
  editor::transactUndoableActionAtomically(
      &document, "Create Emboss preset", [&](editor::Document* actionDoc) {
        return editor::createEmbossPreset(
            actionDoc, &library, "Weathered Brick", emboss, &createdId);
      });
  require(createdId == "weathered_brick" && library.findPreset(createdId),
          "create did not add a uniquely and stably identified preset");
  editor::undo(&document);
  require(!library.findPreset(createdId), "undo did not remove the created preset");
  editor::redo(&document);
  require(library.findPreset(createdId), "redo did not restore the created preset");

  auto* primitive = document.getWorld()->getPrimitive(index);
  editor::transactUndoableAction(
      &document, "Assign floor Emboss preset",
      [&](editor::Document* actionDoc) {
        return editor::setPrimitiveEmbossPreset(
            actionDoc, primitive,
            editor::PrimitiveMaterialSurface::Floor, createdId);
      });
  require(
      primitive->getProperties().floorEmbossPresetId == createdId &&
          primitive->getProperties().ceilingEmbossPresetId.empty() &&
          primitive->getProperties().wallEmbossPresetId.empty(),
      "assignment changed more than the selected Primitive surface");
  editor::undo(&document);
  require(document.getWorld()->getPrimitive(index)->getProperties()
                  .floorEmbossPresetId.empty(),
          "undo did not restore the selected surface assignment");
  editor::redo(&document);
  primitive = document.getWorld()->getPrimitive(index);
  require(primitive->getProperties().floorEmbossPresetId == createdId,
          "redo did not restore the selected surface assignment");

  editor::transactUndoableAction(
      &document, "Assign wall Emboss preset",
      [&](editor::Document* actionDoc) {
        return editor::setPrimitiveEmbossPreset(
            actionDoc, primitive,
            editor::PrimitiveMaterialSurface::Wall, createdId);
      });
  primitive = document.getWorld()->getPrimitive(index);
  auto properties = primitive->getProperties();
  editor::clearUndoHistory();

  editor::transactUndoableActionAtomically(
      &document, "Rename Emboss preset", [&](editor::Document* actionDoc) {
        return editor::renameEmbossPreset(
            actionDoc, &library, createdId, "Ancient Weathered Brick");
      });
  require(library.findPreset(createdId)->displayName == "Ancient Weathered Brick" &&
              document.getWorld()->getPrimitive(index)->getProperties()
                      .wallEmbossPresetId == createdId,
          "rename changed the stable id retained by the Primitive");
  editor::undo(&document);
  require(library.findPreset(createdId)->displayName == "Weathered Brick",
          "undo did not restore the old display name");
  editor::redo(&document);

  auto editedEmboss = emboss;
  editedEmboss.depth = 0.8f;
  editor::transactUndoableActionAtomically(
      &document, "Edit Emboss preset", [&](editor::Document* actionDoc) {
        return editor::editEmbossPreset(
            actionDoc, &library, createdId, editedEmboss);
      });
  require(library.findPreset(createdId)->emboss.depth == 0.8f,
          "edit did not update the preset");
  editor::undo(&document);
  require(library.findPreset(createdId)->emboss.depth == 0.5f,
          "undo did not restore the prior preset values");
  editor::redo(&document);

  auto invalid = editedEmboss;
  invalid.radius = bw::core::EmbossRadiusLimits().maximum + 1.0f;
  bool rejected{false};
  try {
    editor::editEmbossPreset(&document, &library, createdId, invalid);
  } catch (std::invalid_argument const& error) {
    rejected = std::string(error.what()).find("authoring limits") != std::string::npos;
  }
  require(rejected && library.findPreset(createdId)->emboss == editedEmboss,
          "invalid Emboss values were not refused without changing state");

  document.getWorld()->getActiveLayer()->setStepEnabled(0, false);
  std::string blocked;
  auto undoBefore = editor::getUndoLevels();
  auto deleted = editor::transactUndoableActionAtomically(
      &document, "Delete referenced Emboss preset",
      [&](editor::Document* actionDoc) {
        return editor::deleteEmbossPreset(
            actionDoc, &library, createdId, &blocked);
      });
  require(!deleted && blocked.find("Primitive") != std::string::npos &&
              blocked.find("wall") != std::string::npos &&
              editor::getUndoLevels() == undoBefore && library.findPreset(createdId),
          "deletion did not report a reference in a disabled authored step");

  document.getWorld()->getActiveLayer()->setStepEnabled(0, true);
  primitive = document.getWorld()->getPrimitive(index);
  properties = primitive->getProperties();
  properties.floorEmbossPresetId.clear();
  properties.wallEmbossPresetId.clear();
  primitive->setProperties(properties);
  editor::transactUndoableActionAtomically(
      &document, "Delete Emboss preset", [&](editor::Document* actionDoc) {
        return editor::deleteEmbossPreset(actionDoc, &library, createdId);
      });
  require(!library.findPreset(createdId), "unreferenced preset was not deleted");
  editor::undo(&document);
  require(library.findPreset(createdId), "undo did not restore the deleted preset");
  editor::redo(&document);
  require(!library.findPreset(createdId), "redo did not delete the preset again");

  editor::EmbossingCatalogLibrary reloaded;
  reloaded.load(root / "Resources.yaml");
  require(!reloaded.findPreset(createdId) && reloaded.findPreset("existing"),
          "saved YAML does not match the in-memory catalog");
}

}  // namespace

int main() {
  auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  auto root = fs::temp_directory_path() /
              ("boolean-world-emboss-authoring-" + std::to_string(unique));
  fs::create_directories(root);
  try {
    bw::core::LayerBuildStep::registerCoreTypes();

    writeFixture(root);
    authoringPersistsAndIsUndoable(root);
    fs::remove_all(root);
    std::cout << "Embossing catalog authoring coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
