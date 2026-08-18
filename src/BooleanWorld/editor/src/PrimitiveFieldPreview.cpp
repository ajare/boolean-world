#include "PrimitiveFieldPreview.h"

#include <utility>

namespace editor {

void PrimitiveFieldPreview::requestOpen() {
  open = true;
  openRequested = true;
  minimumSpacing = 128.0f;
  maximumSites = 2000;
  seed = 0;
  layout.reset();
  error.clear();
}

void PrimitiveFieldPreview::close() {
  open = false;
  openRequested = false;
  layout.reset();
  error.clear();
}

void PrimitiveFieldPreview::generate(
    bw::core::PrimitiveFieldExtents const& worldExtents) {
  auto result = bw::core::generatePrimitiveFieldLayout(
      {worldExtents, minimumSpacing, static_cast<uint32_t>(maximumSites), seed});
  if (!result.succeeded()) {
    error = std::move(result.error);
    return;
  }

  layout = std::move(result.layout);
  error.clear();
}

PrimitiveFieldPreview& getPrimitiveFieldPreview() {
  static PrimitiveFieldPreview preview;
  return preview;
}

}  // namespace editor
