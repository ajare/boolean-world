#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/RectanglePolygon.h>

#include "Actions.h"
#include "Document.h"
#include "ProcMaterialLibrary.h"
#include "Settings.h"
#include "Undo.h"

spdlog::logger* gLogger = nullptr;
editor::Settings gEditorSettings;

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}
void regenerateWorldData(Document*) {
}
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

std::string catalogYaml(std::string const& id, std::string const& name) {
  return "program3d: world3d\n"
         "program2d: world2d\n"
         "techniqueSchemas:\n"
         "  - materialIndex: 0\n"
         "    parameters:\n"
         "      - name: roughness\n"
         "        min: 0.0\n"
         "        max: 1.0\n"
         "        default: 0.5\n"
         "subMaterials:\n"
         "  - id: \"" +
         id +
         "\"\n"
         "    name: \"" +
         name +
         "\"\n"
         "    materialIndex: 0\n"
         "    params: [0.5]\n"
         "    baseColour: [0.1, 0.2, 0.3]\n";
}

void libraryDiscoversTwoLevelsAndSelectionIsUndoable(fs::path const& root) {
  writeFile(root / "stone.yaml", catalogYaml("stone.rough", "Rough Stone"));
  writeFile(root / "wood.yaml", catalogYaml("wood.oak", "Oak"));
  writeFile(root / "Resources.yaml", R"(Resources:
  Resource:
    - type: "TextFile"
      name: "StoneFile"
      location: "stone.yaml"
    - type: "ProcMaterial"
      name: "Stone Catalog"
      DependentResources:
        DependentResource:
          id: "Yaml"
          ref: "StoneFile"
    - type: "TextFile"
      name: "WoodFile"
      location: "wood.yaml"
    - type: "ProcMaterial"
      name: "Wood Catalog"
      DependentResources:
        DependentResource:
          id: "Yaml"
          ref: "WoodFile"
)");

  editor::ProcMaterialLibrary library;
  library.load(root / "Resources.yaml");
  require(library.catalogs().size() == 2,
          "picker library did not expose both ProcMaterial resources");
  auto const* wood = library.findCatalogForSubMaterial("wood.oak");
  require(wood && wood->resourceName == "Wood Catalog" &&
              wood->data.subMaterials[0].displayName == "Oak",
          "picker library did not expose the resource then its Sub-materials");

  editor::clearUndoHistory();
  editor::Document document;
  document.newDoc();
  auto index = document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));
  auto* primitive = document.getWorld()->getPrimitive(index);

  editor::transactUndoableAction(
      &document, "Set Wall Sub-material",
      [primitive](editor::Document* doc) {
        return editor::setPrimitiveSubMaterial(
            doc, primitive, editor::PrimitiveMaterialSurface::Wall,
            "wood.oak");
      });
  require(document.getWorld()->getPrimitive(index)->getProperties().wallMaterialId ==
              "wood.oak",
          "picker selection did not write the stable Sub-material id");

  editor::undo(&document);
  require(document.getWorld()->getPrimitive(index)->getProperties().wallMaterialId.empty(),
          "undo did not restore the previous wall Sub-material id");
  editor::redo(&document);
  require(document.getWorld()->getPrimitive(index)->getProperties().wallMaterialId ==
              "wood.oak",
          "redo did not restore the selected wall Sub-material id");

  auto const* selected = library.findSubMaterial("wood.oak");
  require(selected && selected->materialIndex == 0 &&
              selected->baseColour == std::array<float, 3>{0.1f, 0.2f, 0.3f},
          "the selected Sub-material data was not available to the renderer");
}

void authoringActionsAreSavedUndoableAndProtectReferences(fs::path const& root) {
  auto& library = editor::procMaterialLibrary();
  library.load(root / "Resources.yaml");
  editor::clearUndoHistory();

  editor::Document document;
  document.newDoc();
  auto index = document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));
  auto* primitive = document.getWorld()->getPrimitive(index);

  std::string createdId;
  editor::transactUndoableActionAtomically(
      &document, "Create Sub-material", [&](editor::Document* actionDoc) {
        return editor::createSubMaterial(
            actionDoc, &library, "Stone Catalog", "Polished Stone", 0, {0.25f},
            {0.2f, 0.3f, 0.4f}, {}, {}, &createdId);
      });
  require(createdId == "polished_stone" && library.findSubMaterial(createdId),
          "create did not add a stably identified Sub-material");
  editor::undo(&document);
  require(!library.findSubMaterial(createdId),
          "undo did not remove the created Sub-material");
  editor::redo(&document);
  require(library.findSubMaterial(createdId),
          "redo did not restore the created Sub-material");
  primitive = document.getWorld()->getPrimitive(index);

  editor::transactUndoableActionAtomically(
      &document, "Assign created Sub-material", [&](editor::Document* actionDoc) {
        return editor::setPrimitiveSubMaterial(
            actionDoc, primitive, editor::PrimitiveMaterialSurface::Wall, createdId);
      });
  primitive = document.getWorld()->getPrimitive(index);
  editor::transactUndoableActionAtomically(
      &document, "Rename Sub-material", [&](editor::Document* actionDoc) {
        return editor::renameSubMaterial(
            actionDoc, &library, createdId, "Mirror-polished Stone");
      });
  require(library.findSubMaterial(createdId)->displayName == "Mirror-polished Stone" &&
              document.getWorld()->getPrimitive(index)->getProperties().wallMaterialId == createdId,
          "rename changed the stable id referenced by a Primitive");
  editor::undo(&document);
  require(library.findSubMaterial(createdId)->displayName == "Polished Stone" &&
              document.getWorld()->getPrimitive(index)->getProperties().wallMaterialId == createdId,
          "undo did not restore the old name while preserving Primitive references");
  editor::redo(&document);
  require(library.findSubMaterial(createdId)->displayName == "Mirror-polished Stone",
          "redo did not restore the renamed Sub-material");

  editor::transactUndoableActionAtomically(
      &document, "Edit Sub-material", [&](editor::Document* actionDoc) {
        return editor::editSubMaterial(
            actionDoc, &library, createdId, {0.8f}, {0.7f, 0.6f, 0.5f}, {},
            {});
      });
  auto const* edited = library.findSubMaterial(createdId);
  require(edited && edited->baseColour == std::array<float, 3>{0.7f, 0.6f, 0.5f} &&
              edited->paramValues[0] == 0.8f,
          "edited Sub-material parameters/colour were not retained");
  editor::undo(&document);
  require(library.findSubMaterial(createdId)->paramValues[0] == 0.25f,
          "undo did not restore the previous Sub-material parameters");
  editor::redo(&document);
  require(library.findSubMaterial(createdId)->paramValues[0] == 0.8f,
          "redo did not restore edited Sub-material parameters");

  bool rejectedOutOfBounds{false};
  try {
    editor::editSubMaterial(
        &document, &library, createdId, {1.1f}, {0.7f, 0.6f, 0.5f}, {}, {});
  } catch (std::invalid_argument const&) {
    rejectedOutOfBounds = true;
  }
  require(rejectedOutOfBounds && library.findSubMaterial(createdId)->paramValues[0] == 0.8f,
          "a parameter outside its Technique schema bounds was accepted");

  std::string blocked;
  auto undoBefore = editor::getUndoLevels();
  auto deleted = editor::transactUndoableActionAtomically(
      &document, "Delete referenced Sub-material", [&](editor::Document* actionDoc) {
        return editor::deleteSubMaterial(actionDoc, &library, createdId, &blocked);
      });
  require(!deleted && blocked.find("Primitive") != std::string::npos &&
              blocked.find("wall") != std::string::npos &&
              editor::getUndoLevels() == undoBefore && library.findSubMaterial(createdId),
          "referenced Sub-material deletion was not refused with a Primitive report");

  primitive = document.getWorld()->getPrimitive(index);
  editor::transactUndoableActionAtomically(
      &document, "Clear Sub-material reference", [&](editor::Document* actionDoc) {
        return editor::setPrimitiveSubMaterial(
            actionDoc, primitive, editor::PrimitiveMaterialSurface::Wall, "");
      });
  editor::transactUndoableActionAtomically(
      &document, "Delete unreferenced Sub-material", [&](editor::Document* actionDoc) {
        return editor::deleteSubMaterial(actionDoc, &library, createdId);
      });
  require(!library.findSubMaterial(createdId),
          "unreferenced Sub-material was not deleted");
  editor::undo(&document);
  require(library.findSubMaterial(createdId),
          "undo did not restore the deleted Sub-material");
  editor::redo(&document);
  require(!library.findSubMaterial(createdId),
          "redo did not delete the Sub-material again");

  editor::ProcMaterialLibrary reloaded;
  reloaded.load(root / "Resources.yaml");
  require(!reloaded.findSubMaterial(createdId),
          "authoring changes were not saved to the ProcMaterial YAML file");
}
}  // namespace

int main() {
  auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path root = fs::temp_directory_path() /
                  ("boolean-world-sub-material-picker-" + std::to_string(unique));
  fs::create_directories(root);
  try {
    libraryDiscoversTwoLevelsAndSelectionIsUndoable(root);
    authoringActionsAreSavedUndoableAndProtectReferences(root);
    fs::remove_all(root);
    std::cout << "Sub-material picker coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
