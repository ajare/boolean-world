#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderHistoryView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto history = getActionHistory();

  if (history.empty()) {
    return;
  }

  // Get number of undos and redos ahead of time
  uint32_t undoCount = 0;
  for (; undoCount < (uint32_t)history.size(); ++undoCount) {
    if (!history[undoCount].isUndo) {
      break;
    }
  }

  uint32_t redoCount = (uint32_t)history.size() - undoCount;

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.7f, 1));

  for (uint32_t i = 0; i < (uint32_t)history.size(); ++i) {
    auto const& item = history[i];
    bool isUndo = i < undoCount;

    ImGui::PushID(i);

    if (i == undoCount) {
      ImGui::Separator();

      ImGui::PopStyleColor();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 1.0f, 0.7f, 1));
    }

    if (ImGui::Button(isUndo ? ICON_FA_UNDO : ICON_FA_REDO)) {
      // Work out how many to go back or forward
      if (isUndo) {
        undo(doc, undoCount - i);
      } else {
        redo(doc, (i + 1) - undoCount);
      }
    }

    ImGui::SameLine();
    auto const& command = commandInfo(item.command);
    ImGui::TextUnformatted(command.name.data(), command.name.data() + command.name.size());

    ImGui::PopID();
  }

  ImGui::PopStyleColor();
}

void renderHistoryPanel(ViewContext& context) {
  if (ImGui::Begin("History")) {
    renderHistoryView(context);
  }

  ImGui::End();
}


}  // namespace editor
