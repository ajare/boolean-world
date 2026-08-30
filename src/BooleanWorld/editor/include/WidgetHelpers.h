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

// A pattern picker and the shape parameters that pattern actually uses, all
// bounded by core's own authoring limits so what can be authored here is
// exactly what will deserialize again. Shared by global Emboss-preset
// authoring panels.
void EmbossFields(bw::core::EmbossData& emboss);

// Procedural Chip eligibility, count, spacing, and size variation. Shared by
// both Sub-material authoring surfaces so neither silently drops a field.
void ChipFields(bw::core::ChipGenerationParameters& chip);

}  // namespace widgets

}  // namespace editor
