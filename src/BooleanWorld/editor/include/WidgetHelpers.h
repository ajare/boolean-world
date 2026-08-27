#pragma once

#include <string>

#include <core/Emboss.h>
#include <core/SubMaterial.h>

#include "imgui.h"

namespace editor {
namespace widgets {

void PushDisabled();

void PopDisabled();

bool ToggleButton(const char* str_id, const char* title, bool v);

bool ToggleButton(char const* str_id, const char* title, bool* v);

void HelpMarker(char const* desc);

// std::string-backed equivalents of ImGui::InputText / InputTextMultiline,
// using ImGuiInputTextFlags_CallbackResize so the backing string can grow
// past whatever its current capacity happens to be (avoids fixed-size
// stack buffers that overflow on long input).
bool InputText(
    const char* label, std::string* str, ImGuiInputTextFlags flags = 0,
    ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);

bool InputTextMultiline(
    const char* label, std::string* str, const ImVec2& size = ImVec2(0, 0),
    ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr,
    void* userData = nullptr);

// The relief a Sub-material embosses into every surface it is applied to:
// a pattern picker and the shape parameters that pattern actually uses, all
// bounded by core's own authoring limits so what can be authored here is
// exactly what will deserialize again. Shared by the Sub-material picker's
// create/edit popups and the 3D preview's selected-surface editor, so neither
// can silently drop what the other authored.
void EmbossFields(bw::core::EmbossData& emboss);

// Chip depth and reach: independent of Embossing, and like it bounded by
// their own authoring limits rather than by a Technique schema - see
// bw::core::ChipDepthLimits/ChipReachLimits. Shared for the same reason
// EmbossFields is.
void ChipFields(float& chipDepth, float& chipReach);

}  // namespace widgets

}  // namespace editor
