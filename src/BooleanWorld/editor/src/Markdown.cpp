#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <Shellapi.h>
#include <string>

#include "imgui.h"
#include "imgui_markdown.h"
#include "IconsFontAwesome5.h"

#include "Markdown.h"

#include <spdlog/spdlog.h>

extern spdlog::logger* gLogger;

using namespace std;

static ImGui::MarkdownConfig mdConfig;

void linkCallback(ImGui::MarkdownLinkCallbackData data) {
  if (data.isImage) {
    return;
  }
  string url(data.link, data.linkLength);
  if (!url.starts_with("http://") && !url.starts_with("https://")) {
    gLogger->warn("Refusing to open a help link that is not http(s): {}", url);
    return;
  }
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline ImGui::MarkdownImageData imageCallback(ImGui::MarkdownLinkCallbackData data) {
  // In your application you would load an image based on data input. Here we just use the imgui font texture.
  ImTextureID image = ImGui::GetIO().Fonts->TexRef.GetTexID();
  // > C++14 can use ImGui::MarkdownImageData imageData{ true, false, image, ImVec2( 40.0f, 20.0f ) };
  ImGui::MarkdownImageData imageData;
  imageData.isValid = true;
  imageData.useLinkCallback = false;
  imageData.user_texture_id = image;
  imageData.size = ImVec2(40.0f, 20.0f);

  // For image resize when available size.x > image width, add
  ImVec2 const contentSize = ImGui::GetContentRegionAvail();
  if (imageData.size.x > contentSize.x) {
    float const ratio = imageData.size.y / imageData.size.x;
    imageData.size.x = contentSize.x;
    imageData.size.y = contentSize.x * ratio;
  }

  return imageData;
}

void markdownFormatCallback(const ImGui::MarkdownFormatInfo& startMarkdownFormatInfo, bool start) {
  ImGui::defaultMarkdownFormatCallback(startMarkdownFormatInfo, start);

  switch (startMarkdownFormatInfo.type) {
    case ImGui::MarkdownFormatType::HEADING:
      switch (startMarkdownFormatInfo.level) {
        case 2:
          if (start) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
          } else {
            ImGui::PopStyleColor();
          }
          break;
        case 3:
          if (start) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.7f, 1.0f));
          } else {
            ImGui::PopStyleColor();
          }
          break;

        default:
          break;
      }
      break;

    default:
      break;
  }
}

void renderMarkdown(string const& content) {
  auto font = ImGui::GetIO().Fonts->Fonts[0];

  mdConfig.linkCallback = linkCallback;
  mdConfig.tooltipCallback = nullptr;
  mdConfig.imageCallback = imageCallback;
  mdConfig.linkIcon = ICON_FA_LINK;

  mdConfig.headingFormats[0] = {font, true};
  mdConfig.headingFormats[1] = {font, true};
  mdConfig.headingFormats[2] = {font, false};

  mdConfig.userData = nullptr;
  mdConfig.formatCallback = markdownFormatCallback;

  ImGui::Markdown(content.c_str(), content.length(), mdConfig);
}
