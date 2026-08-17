#pragma once

#include "imgui/imgui.h"
#include "imgui/implot.h"

class ImGuiDllBoundaryState {
public:
  ImGuiDllBoundaryState(ImGuiContext* imGuiContext, ImPlotContext* imPlotContext,
                        ImGuiMemAllocFunc allocFunc, ImGuiMemFreeFunc freeFunc,
                        void* userData)
      : mPreviousImGuiContext{ImGui::GetCurrentContext()},
        mPreviousImPlotContext{ImPlot::GetCurrentContext()} {
    ImGui::GetAllocatorFunctions(
        &mPreviousAllocFunc, &mPreviousFreeFunc, &mPreviousUserData);

    ImGui::SetCurrentContext(imGuiContext);
    ImPlot::SetCurrentContext(imPlotContext);
    ImGui::SetAllocatorFunctions(allocFunc, freeFunc, userData);
  }

  ~ImGuiDllBoundaryState() {
    ImGui::SetAllocatorFunctions(
        mPreviousAllocFunc, mPreviousFreeFunc, mPreviousUserData);
    ImPlot::SetCurrentContext(mPreviousImPlotContext);
    ImGui::SetCurrentContext(mPreviousImGuiContext);
  }

  ImGuiDllBoundaryState(ImGuiDllBoundaryState const&) = delete;
  ImGuiDllBoundaryState& operator=(ImGuiDllBoundaryState const&) = delete;

private:
  ImGuiContext* mPreviousImGuiContext;
  ImPlotContext* mPreviousImPlotContext;
  ImGuiMemAllocFunc mPreviousAllocFunc;
  ImGuiMemFreeFunc mPreviousFreeFunc;
  void* mPreviousUserData;
};
