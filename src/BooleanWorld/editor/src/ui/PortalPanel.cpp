#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderPortalsView(ViewContext& context) {
  auto* doc = context.doc;
  auto* layer = doc->getWorld()->getActiveLayer();

  if (ImGui::Button("Create Portal pair")) {
    auto centre = doc->getGhost()->getPosition();
    bw::core::AuthoredAperture first{centre, 16.0f, 0.0f, 24.0f};
    bw::core::AuthoredAperture second{
        centre + wp::Vector2{32.0f, 0.0f}, 16.0f, 0.0f, 24.0f};
    transactUndoableActionAtomically(
        doc, CommandId::CreatePortalPair,
        [=](Document* document) {
          return createPortalPair(document, layer, first, second);
        });
  }

  for (auto const& pair : layer->getPortalPairs()) {
    ImGui::PushID(static_cast<int>(pair.getId()));
    ImGui::Text("Pair %u", pair.getId());
    ImGui::SameLine();
    for (uint32_t endpointIndex = 0; endpointIndex < 2; ++endpointIndex) {
      if (endpointIndex != 0) ImGui::SameLine();
      auto const selected =
          doc->getSelectedPortalLayerId() == layer->getId() &&
          doc->getSelectedPortalPairId() == pair.getId() &&
          doc->getSelectedPortalEndpointIndex() == endpointIndex;
      if (selected) ImGui::PushStyleColor(
          ImGuiCol_Button, ImVec4{0.85f, 0.55f, 0.12f, 1.0f});
      auto label = format("Endpoint {}", endpointIndex + 1);
      if (ImGui::Button(label.c_str())) {
        transactUndoableAction(
            doc, CommandId::SelectPortalEndpoint,
            [layerId = layer->getId(), pairId = pair.getId(), endpointIndex](
                Document* transactionDocument) {
              return selectPortalEndpoint(
                  transactionDocument, layerId, pairId, endpointIndex);
            });
      }
      if (selected) ImGui::PopStyleColor();
    }
    ImGui::PopID();
  }

  if (doc->getSelectedPortalLayerId() != layer->getId()) return;
  auto const pairId = doc->getSelectedPortalPairId();
  auto const endpointIndex = doc->getSelectedPortalEndpointIndex();
  auto const* pair = layer->getPortalPair(pairId);
  if (!pair || endpointIndex >= 2) return;

  ImGui::SeparatorText("Selected Portal endpoint");
  auto const aperture = pair->getEndpoint(endpointIndex).getAperture();
  float centre[2]{aperture.centre.x, aperture.centre.y};
  if (ImGui::InputFloat2("Centre", centre)) {
    auto position = wp::Vector2{centre[0], centre[1]};
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalEndpointPosition,
        [=](Document* document) {
          return setPortalEndpointPosition(
              document, layer, pairId, endpointIndex, position);
        });
  }
  auto width = aperture.width;
  if (ImGui::InputFloat("Width", &width) && width > 0.0f) {
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalEndpointWidth,
        [=](Document* document) {
          return setPortalEndpointWidth(
              document, layer, pairId, endpointIndex, width);
        });
  }
  float vertical[2]{aperture.bottom, aperture.top};
  if (ImGui::InputFloat2("Bottom / top", vertical) &&
      vertical[1] > vertical[0]) {
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalEndpointVerticalBounds,
        [=](Document* document) {
          return setPortalEndpointVerticalBounds(
              document, layer, pairId, endpointIndex, vertical[0],
              vertical[1]);
        });
  }

  auto const* resolved = context.worldData
                             ? context.worldData->findPortalPair(
                                   layer->getId(), pairId)
                             : nullptr;
  if (!resolved) {
    ImGui::TextDisabled("Waiting for this Layer's generation.");
  } else {
    auto const& endpoint = resolved->endpoints[endpointIndex];
    auto const diagnostic = endpoint.diagnostic;
    auto const colour = resolved->active
                            ? ImVec4{0.3f, 0.9f, 0.9f, 1.0f}
                            : ImVec4{1.0f, 0.3f, 0.25f, 1.0f};
    ImGui::TextColored(
        colour, "%s",
        bw::core::PortalResolutionDiagnosticText(diagnostic).data());
    if (endpoint.resolved) {
      ImGui::Text(
          "Resolved width %.3f; walls %zu", endpoint.aperture.width,
          endpoint.aperture.wallIndices.size());
    }
  }

  if (ImGui::Button("Delete pair")) {
    transactUndoableActionAtomically(
        doc, CommandId::DeletePortalPair,
        [=](Document* document) {
          return deletePortalPair(document, layer, pairId);
        });
  }
}

}  // namespace editor
