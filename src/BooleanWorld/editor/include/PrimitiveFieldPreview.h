#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <core/PrimitiveFieldLayout.h>

namespace editor {

struct PrimitiveFieldPreview {
  bool open{false};
  bool openRequested{false};
  float minimumSpacing{128.0f};
  int maximumSites{2000};
  int seed{0};
  int lloydIterations{5};
  std::optional<bw::core::PrimitiveFieldLayout> layout;
  std::string error;

  void requestOpen();
  void close();
  void invalidateLayout();
  void generate(bw::core::PrimitiveFieldExtents const& worldExtents);
};

PrimitiveFieldPreview& getPrimitiveFieldPreview();

}  // namespace editor
