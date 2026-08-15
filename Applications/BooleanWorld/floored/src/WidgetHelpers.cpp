#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome5.h"

#include "WidgetHelpers.h"

namespace floored {
namespace widgets {

void PushDisabled() {
  ImGuiContext& g = *GImGui;
  if ((g.CurrentItemFlags & ImGuiItemFlags_Disabled) == 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g.Style.Alpha * 0.6f);
  }

  ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
}

void PopDisabled() {
  ImGui::PopItemFlag();

  ImGuiContext& g = *GImGui;
  if ((g.CurrentItemFlags & ImGuiItemFlags_Disabled) == 0) {
    ImGui::PopStyleVar();
  }
}

bool ToggleButton(const char* str_id, const char* title, bool v) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  float height = ImGui::GetFrameHeight();
  float width = height * 1.55f;
  float radius = height * 0.50f;

  ImGui::InvisibleButton(str_id, ImVec2(width, height));

  bool clicked = ImGui::IsItemClicked();

  float t = v ? 1.0f : 0.0f;

  ImGuiContext& g = *GImGui;
  float ANIM_SPEED = 0.08f;
  if (g.LastActiveId == g.CurrentWindow->GetID(str_id))  // && g.LastActiveIdTimer < ANIM_SPEED)
  {
    float t_anim = ImSaturate(g.LastActiveIdTimer / ANIM_SPEED);
    t = v ? (t_anim) : (1.0f - t_anim);
  }

  ImU32 col_bg;
  if (ImGui::IsItemHovered())
    col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.78f, 0.78f, 0.78f, 1.0f), ImVec4(0.64f, 0.83f, 0.34f, 1.0f), t));
  else
    col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), ImVec4(0.56f, 0.83f, 0.26f, 1.0f), t));

  draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
  draw_list->AddCircleFilled(ImVec2(p.x + radius + t * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

  ImGui::SameLine();
  ImGui::Text(title);

  return clicked;
}

bool ToggleButton(char const* str_id, const char* title, bool* v) {
  auto clicked = ToggleButton(str_id, title, *v);

  if (clicked) {
    *v = !*v;
  }

  return clicked;
}

void HelpMarker(char const* desc) {
  ImGui::TextDisabled(ICON_FA_QUESTION_CIRCLE);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 65.0f);
    ImGui::TextUnformatted(desc);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

}  // namespace widgets
}  // namespace floored