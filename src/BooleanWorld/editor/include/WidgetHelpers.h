#pragma once

#include <cstdint>

#include "imgui.h"

namespace bw {
namespace core {
class World;
}
}  // namespace bw

namespace editor {
namespace widgets {

void PushDisabled();

void PopDisabled();

bool ToggleButton(const char* str_id, const char* title, bool v);

bool ToggleButton(char const* str_id, const char* title, bool* v);

void HelpMarker(char const* desc);

// A combo box listing world's Layers by name, in World's own order.
// Returns the id of whichever Layer ends up selected after this call -
// currentLayerId itself if the user didn't pick a different one this frame.
uint32_t LayerPicker(char const* label, bw::core::World const* world, uint32_t currentLayerId);

}  // namespace widgets

}  // namespace editor
