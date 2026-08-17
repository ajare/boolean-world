#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "ImGuiDllBoundaryState.h"

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void* hostAlloc(size_t size, void*) {
  return std::malloc(size);
}

void hostFree(void* pointer, void*) {
  std::free(pointer);
}

void* callerAlloc(size_t size, void*) {
  return std::malloc(size);
}

void callerFree(void* pointer, void*) {
  std::free(pointer);
}

void restoresCallerStateAfterCrossingDllBoundary() {
  int hostUserData = 0;
  int callerUserData = 0;

  ImGui::SetAllocatorFunctions(hostAlloc, hostFree, &hostUserData);
  ImGuiContext* hostImGuiContext = ImGui::CreateContext();
  ImGuiContext* callerImGuiContext = ImGui::CreateContext();
  ImPlotContext* hostImPlotContext = ImPlot::CreateContext();
  ImPlotContext* callerImPlotContext = ImPlot::CreateContext();

  ImGui::SetCurrentContext(hostImGuiContext);
  ImPlot::SetCurrentContext(hostImPlotContext);

  {
    ImGuiDllBoundaryState boundaryState{
        callerImGuiContext, callerImPlotContext,
        callerAlloc, callerFree, &callerUserData};

    require(ImGui::GetCurrentContext() == callerImGuiContext,
            "DLL did not install the caller ImGui context");
    require(ImPlot::GetCurrentContext() == callerImPlotContext,
            "DLL did not install the caller ImPlot context");

    ImGuiMemAllocFunc allocFunc;
    ImGuiMemFreeFunc freeFunc;
    void* userData;
    ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
    require(allocFunc == callerAlloc && freeFunc == callerFree &&
                userData == &callerUserData,
            "DLL did not install the caller ImGui allocators");
  }

  ImGuiMemAllocFunc allocFunc;
  ImGuiMemFreeFunc freeFunc;
  void* userData;
  ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);

  require(ImGui::GetCurrentContext() == hostImGuiContext,
          "DLL left its ImGui context installed in the host");
  require(ImPlot::GetCurrentContext() == hostImPlotContext,
          "DLL left its ImPlot context installed in the host");
  require(allocFunc == hostAlloc && freeFunc == hostFree &&
              userData == &hostUserData,
          "DLL left its ImGui allocators installed in the host");

  ImPlot::DestroyContext(callerImPlotContext);
  ImPlot::DestroyContext(hostImPlotContext);
  ImGui::DestroyContext(callerImGuiContext);
  ImGui::DestroyContext(hostImGuiContext);
}

}  // namespace

int main() {
  try {
    restoresCallerStateAfterCrossingDllBoundary();
    std::cout << "ImGui DLL-boundary state regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
