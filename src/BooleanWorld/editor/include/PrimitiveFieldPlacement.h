#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <core/PrimitiveFieldLayout.h>

#include "PrimitiveFieldPreview.h"

namespace bw::core {
class Primitive;
class World;
}  // namespace bw::core

namespace editor {
class Document;
struct Settings;

struct PrimitiveFieldPlacementResult {
  bool placed{false};
  std::string error;
};

// Testable insertion seam. A successful inserter must append the supplied
// primitive and return its index; ownership transfers to World on success.
using PrimitiveFieldInserter =
    std::function<uint32_t(bw::core::World&, bw::core::Primitive*)>;

[[nodiscard]] PrimitiveFieldPlacementResult placePrimitiveField(
    Document* document,
    bw::core::PrimitiveFieldLayout const& layout,
    std::vector<PrimitiveFieldPrimitivePreview> const& primitives,
    Settings const& settings,
    PrimitiveFieldInserter inserter = {});

}  // namespace editor
