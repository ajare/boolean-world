#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include "Document.h"
#include "Tiled.h"

spdlog::logger* gLogger = nullptr;

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

}  // namespace

int main() {
  try {
    changingSelectedPrimitiveIndicesDoesNotWriteIntoAnInputRange();
    primitiveHoverQueriesAreSafeWithoutAnActiveDocument();
    std::cout << "Document selection and hover queries passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
