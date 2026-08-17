#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/RectanglePolygon.h>

#include "Document.h"
#include "Tiled.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();

void openTiledPrefabFile(std::string const&, std::shared_ptr<bw::core::World>) {
}

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
    openingADocumentReplacesTheActiveDocument();
    std::cout << "Document selection and hover queries passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
