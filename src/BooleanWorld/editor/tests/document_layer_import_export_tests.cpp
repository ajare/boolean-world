#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/Layer.h>
#include <core/RectanglePolygon.h>
#include <core/WorldTriggerLine.h>

#include "Document.h"
#include "EditorException.h"
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

std::filesystem::path tempPath(std::string const& name) {
  return std::filesystem::temp_directory_path() / name;
}

void aLayerExportsAndImportsThroughDotLayerBinaryFile() {
  auto const path = tempPath("boolean-world-layer-export-test.layer");

  editor::Document document;
  document.newDoc();
  auto* sourceLayer = document.getWorld()->addLayer("Exported");
  sourceLayer->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f));

  document.exportLayer(sourceLayer, path.string());

  auto* imported = document.importLayer(path.string());

  require(imported != nullptr, "importing a .layer file returned no Layer");
  require(imported->getName() == "Exported", "a .layer file did not keep the exported Layer's name");
  require(imported->getNumPrimitives() == 1,
          "a .layer file did not keep the exported Layer's Primitives");
  require(document.getWorld()->getNumLayers() == 3,
          "importing a Layer did not add it to the current World's Layer collection");

  std::filesystem::remove(path);
}

void aLayerExportsAndImportsThroughDotLayerYamlFile() {
  auto const path = tempPath("boolean-world-layer-export-test.layer.yaml");

  editor::Document document;
  document.newDoc();
  auto* sourceLayer = document.getWorld()->addLayer("Background");
  sourceLayer->addTriggerLine(new bw::core::WorldTriggerLine({-1.0f, 5.0f}, {1.0f, 5.0f}));

  document.exportLayer(sourceLayer, path.string());

  auto* imported = document.importLayer(path.string());

  require(imported != nullptr, "importing a .layer.yaml file returned no Layer");
  require(imported->getName() == "Background",
          "a .layer.yaml file did not keep the exported Layer's name");
  require(imported->getNumTriggerLines() == 1,
          "a .layer.yaml file did not keep the exported Layer's WorldTriggerLines");

  std::filesystem::remove(path);
}

void importingALayerWhoseIdCollidesGetsANonCollidingOne() {
  auto const path = tempPath("boolean-world-layer-collision-test.layer");

  editor::Document document;
  document.newDoc();

  // Export the World's own default Layer (id 0), then import it straight
  // back into the same World, which still has a Layer with that id.
  auto* originalLayer = document.getWorld()->getActiveLayer();
  auto const originalId = originalLayer->getId();
  document.exportLayer(originalLayer, path.string());

  auto* imported = document.importLayer(path.string());

  require(imported != nullptr, "importing a colliding .layer file returned no Layer");
  require(imported->getId() != originalId,
          "importing a Layer whose id collided with an existing one kept the colliding id");
  require(document.getWorld()->getLayers()[0]->getId() == originalId,
          "importing a colliding Layer disturbed the World's existing Layer's id");

  std::filesystem::remove(path);
}

void anUnsupportedExtensionIsRejectedForExportAndImport() {
  editor::Document document;
  document.newDoc();

  bool exportThrew{false};
  try {
    document.exportLayer(document.getWorld()->getActiveLayer(), tempPath("layer.world").string());
  } catch (EditorException const&) {
    exportThrew = true;
  }
  require(exportThrew, "exporting a Layer to an unsupported extension did not fail clearly");

  bool importThrew{false};
  try {
    document.importLayer(tempPath("layer.world").string());
  } catch (EditorException const&) {
    importThrew = true;
  }
  require(importThrew, "importing an unsupported extension did not fail clearly");
}

void aDotLayerYamlFileIsNotConfusedWithAPlainYamlFile() {
  auto const layerYamlPath = tempPath("boolean-world-layer-vs-yaml-test.layer.yaml");
  auto const plainYamlPath = tempPath("boolean-world-layer-vs-yaml-test.yaml");

  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->addLayer("Distinguishable");

  // Exporting a Layer to a plain ".yaml" path (no ".layer" before it) is not
  // a supported Layer file - only ".layer"/".layer.yaml" dispatch here.
  bool threw{false};
  try {
    document.exportLayer(layer, plainYamlPath.string());
  } catch (EditorException const&) {
    threw = true;
  }
  require(threw, "a plain .yaml path was accepted as a Layer export target");

  document.exportLayer(layer, layerYamlPath.string());
  require(std::filesystem::exists(layerYamlPath), "exporting to .layer.yaml did not write a file");

  std::filesystem::remove(layerYamlPath);
}

}  // namespace

int main() {
  try {
    aLayerExportsAndImportsThroughDotLayerBinaryFile();
    aLayerExportsAndImportsThroughDotLayerYamlFile();
    importingALayerWhoseIdCollidesGetsANonCollidingOne();
    anUnsupportedExtensionIsRejectedForExportAndImport();
    aDotLayerYamlFileIsNotConfusedWithAPlainYamlFile();
    std::cout << "Document exports and imports standalone Layer files\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
