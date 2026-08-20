#include <iostream>
#include <stdexcept>
#include <string>

#include <core/MaterialDefinition.h>
#include <spdlog/spdlog.h>

#include "Actions.h"
#include "Settings.h"
#include "UiHelpers.h"

spdlog::logger* gLogger = nullptr;
editor::Settings gEditorSettings;

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}
}  // namespace editor

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void editorMaterialColourChangesArePackedForRendering() {
  bw::core::MaterialDefinitionData material{};

  editor::setPrimitiveDefaultMaterial(0, &material);
  require(material.packedColour() == 0xff332e2eu,
          "the Marble default material colour was not packed for rendering");

  material.baseColour = {0.0f, 0.5f, 1.0f};
  require(material.packedColour() == 0xffff8000u,
          "an edited material colour was not repacked for rendering");
}

}  // namespace

int main() {
  try {
    editorMaterialColourChangesArePackedForRendering();
    std::cout << "Material colour packing passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
