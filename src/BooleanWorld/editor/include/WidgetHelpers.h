#pragma once

#include <string>

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

}  // namespace widgets

}  // namespace editor
