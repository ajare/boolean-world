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
#include "PrimitivePreviewGeometry.h"
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
         "    parameters: []\n"
         "subMaterials:\n"
         "  - id: \"" + id + "\"\n"
         "    name: \"" + name + "\"\n"
         "    materialIndex: 0\n"
         "    params: []\n"
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

  auto preview = editor::extrudePrimitiveForPreview(
      *document.getWorld()->getPrimitive(index), &library);
  require(preview.wallMaterial.index == 0 &&
              preview.wallMaterial.definition.baseColour ==
                  std::array<float, 3>{0.1f, 0.2f, 0.3f},
          "editor preview did not resolve the selected Sub-material data");
}
}  // namespace

int main() {
  auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path root = fs::temp_directory_path() /
                  ("boolean-world-sub-material-picker-" + std::to_string(unique));
  fs::create_directories(root);
  try {
    libraryDiscoversTwoLevelsAndSelectionIsUndoable(root);
    fs::remove_all(root);
    std::cout << "Sub-material picker coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
