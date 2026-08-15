#define NOMINMAX
#include <Windows.h>
#undef NOMINMAX

#include <nfd/nfd.h>

#include "imgui.h"

#include "AppHelpers.h"
#include "ExitApplicationException.h"

namespace floored {
using namespace std;

void openWorld(Document* doc) {
  vector<pair<string, string>> extensions = {
      {"YAML", "yaml"}};

  auto numExtensions = extensions.size();

  auto filters = new nfdfilteritem_t[numExtensions];
  for (uint32_t i = 0; i < numExtensions; ++i) {
    filters[i] = {extensions[i].first.c_str(), extensions[i].second.c_str()};
  }

  nfdchar_t* outPath;
  auto res = NFD_OpenDialog(&outPath, filters, (nfdfiltersize_t)numExtensions, nullptr);

  if (res == NFD_OKAY) {
    string filepath(outPath);
    bool ok = doc->openWorld(filepath);

    NFD_FreePath(outPath);

    if (!ok) {
      ImGui::OpenPopup("Open file failed");
    }
  }

  delete[] filters;
}

void exitApp(Document* doc) {
  throw ExitApplicationException(0, "Exit");
}

}  // namespace floored