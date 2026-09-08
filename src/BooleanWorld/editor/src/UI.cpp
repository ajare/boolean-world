#define NOMINMAX

#include "UiInternal.h"

namespace editor {

void renderWidgets(
    Document* doc,
    Settings& settings,
    bw::core::WorldData const* worldData,
    double globalTime) {
  ViewContext context{doc, settings, worldData, globalTime};
  renderEditor(context);
}

}  // namespace editor
