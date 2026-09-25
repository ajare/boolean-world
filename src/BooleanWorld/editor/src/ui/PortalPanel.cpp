#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderPortalsView(ViewContext& context) {
  auto* doc = context.doc;
  auto* layer = doc->getWorld()->getActiveLayer();

  if (ImGui::Button("Create Portal (Mirror)")) {
    transactUndoableActionAtomically(doc, CommandId::CreatePortal,
        [=](Document* document) { return createPortal(document, layer); });
  }
  for (auto const& portal : layer->getPortals()) {
    auto label = format("{}{}###portal{}", portal.getName(),
        portal.getTargetId() == portal.getId() ? " — Mirror" : "", portal.getId());
    if (ImGui::Selectable(label.c_str(),
            doc->getSelectedPortalLayerId() == layer->getId() &&
            doc->getSelectedPortalId() == portal.getId())) {
      transactUndoableAction(doc, CommandId::SelectPortalEndpoint,
          [layerId = layer->getId(), id = portal.getId()](Document* document) {
            return selectPortalEndpoint(document, layerId,
                bw::core::IndependentPortalLoopId, id);
          });
    }
  }

  if (ImGui::Button("Create Portal loop")) {
    auto centre = doc->getGhost()->getPosition();
    bw::core::AuthoredAperture first{centre, 16.0f, 0.0f, 24.0f};
    bw::core::AuthoredAperture second{
        centre + wp::Vector2{32.0f, 0.0f}, 16.0f, 0.0f, 24.0f};
    transactUndoableActionAtomically(
        doc, CommandId::CreatePortalLoop,
        [=](Document* document) {
          return createPortalLoop(document, layer, first, second);
        });
  }

  for (auto const& portalLoop : layer->getPortalLoops()) {
    ImGui::PushID(static_cast<int>(portalLoop.getId()));
    ImGui::Text("Loop %u", portalLoop.getId());
    ImGui::SameLine();
    auto firstEndpoint = true;
    for (auto endpointId : portalLoop.getTraversalOrder()) {
      if (!firstEndpoint) ImGui::SameLine();
      firstEndpoint = false;
      auto const selected =
          doc->getSelectedPortalLayerId() == layer->getId() &&
          doc->getSelectedPortalLoopId() == portalLoop.getId() &&
          doc->getSelectedPortalEndpointId() == endpointId;
      if (selected) ImGui::PushStyleColor(
          ImGuiCol_Button, ImVec4{0.85f, 0.55f, 0.12f, 1.0f});
      auto label = format("Endpoint {}", endpointId);
      if (ImGui::Button(label.c_str())) {
        transactUndoableAction(
            doc, CommandId::SelectPortalEndpoint,
            [layerId = layer->getId(), loopId = portalLoop.getId(), endpointId](
                Document* transactionDocument) {
              return selectPortalEndpoint(
                  transactionDocument, layerId, loopId, endpointId);
            });
      }
      if (selected) ImGui::PopStyleColor();
    }
    ImGui::PopID();
  }

  if (doc->getSelectedPortalLayerId() != layer->getId()) return;
  auto const loopId = doc->getSelectedPortalLoopId();
  auto const endpointId = doc->getSelectedPortalEndpointId();
  auto const* portalLoop = layer->getPortalLoop(loopId);
  auto const* authored = layer->findAuthoredPortalAperture(loopId, endpointId);
  if (!authored) return;

  ImGui::SeparatorText(portalLoop ? "Selected Portal endpoint" : "Selected Portal");
  if (portalLoop) {
    ImGui::Text("Destination: endpoint %u", portalLoop->getNextEndpointId(endpointId));
  } else {
    auto const* portal = layer->getPortal(endpointId);
    auto targetLabel = [&](bw::core::Portal const& target) {
      return target.getName() + (target.getId() == endpointId ? " (self — Mirror)" : "");
    };
    auto preview = targetLabel(*layer->getPortal(portal->getTargetId()));
    if (ImGui::BeginCombo("Target", preview.c_str())) {
      for (auto const& target : layer->getPortals()) {
        auto label = targetLabel(target);
        if (ImGui::Selectable(label.c_str(), target.getId() == portal->getTargetId())) {
          auto targetId = target.getId();
          transactUndoableActionAtomically(doc, CommandId::SetPortalTarget,
              [=](Document* document) {
                return setPortalTarget(document, layer, endpointId, targetId);
              });
        }
      }
      ImGui::EndCombo();
    }
  }
  auto const aperture = *authored;
  float centre[2]{aperture.centre.x, aperture.centre.y};
  if (ImGui::InputFloat2("Centre", centre)) {
    auto position = wp::Vector2{centre[0], centre[1]};
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalEndpointPosition,
        [=](Document* document) {
          return setPortalEndpointPosition(
              document, layer, loopId, endpointId, position);
        });
  }
  auto width = aperture.width;
  if (ImGui::InputFloat("Width", &width) && width > 0.0f) {
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalEndpointWidth,
        [=](Document* document) {
          return setPortalEndpointWidth(
              document, layer, loopId, endpointId, width);
        });
  }
  float vertical[2]{aperture.bottom, aperture.top};
  if (ImGui::InputFloat2("Bottom / top", vertical) &&
      vertical[1] > vertical[0]) {
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalEndpointVerticalBounds,
        [=](Document* document) {
          return setPortalEndpointVerticalBounds(
              document, layer, loopId, endpointId, vertical[0],
              vertical[1]);
        });
  }

  if (portalLoop) {
    auto addedAperture = aperture;
    addedAperture.centre += wp::Vector2{32.0f, 0.0f};
    if (ImGui::Button("Add after")) {
      transactUndoableActionAtomically(
          doc, CommandId::AddPortalEndpoint,
          [=](Document* document) {
            return addPortalEndpoint(
                document, layer, loopId, endpointId, addedAperture);
          });
    }
    ImGui::SameLine();
    if (ImGui::Button("Move earlier")) {
      transactUndoableActionAtomically(
          doc, CommandId::MovePortalEndpointEarlier,
          [=](Document* document) {
            return movePortalEndpointEarlier(
                document, layer, loopId, endpointId);
          });
    }
    ImGui::SameLine();
    if (ImGui::Button("Move later")) {
      transactUndoableActionAtomically(
          doc, CommandId::MovePortalEndpointLater,
          [=](Document* document) {
            return movePortalEndpointLater(
                document, layer, loopId, endpointId);
          });
    }
    if (portalLoop->getEndpoints().size() > 2 && ImGui::Button("Delete endpoint")) {
      transactUndoableActionAtomically(
          doc, CommandId::DeletePortalEndpoint,
          [=](Document* document) {
            return deletePortalEndpoint(
                document, layer, loopId, endpointId);
          });
    }
  }

  auto const* resolved = context.worldData
                             ? context.worldData->findPortalLoop(
                                   layer->getId(), loopId, endpointId)
                             : nullptr;
  if (!resolved) {
    ImGui::TextDisabled("Waiting for this Layer's generation.");
  } else {
    auto const* endpoint = context.worldData->findPortalEndpoint(
        layer->getId(), loopId, endpointId);
    if (!endpoint) {
      ImGui::TextDisabled("Waiting for endpoint generation.");
      return;
    }
    auto const diagnostic = endpoint->diagnostic;
    if (!portalLoop) {
      auto const graphValid = endpoint->targetGraphDiagnostic ==
                              bw::core::PortalTargetGraphDiagnostic::None;
      ImGui::TextColored(
          graphValid ? ImVec4{0.3f, 0.9f, 0.9f, 1.0f}
                     : ImVec4{1.0f, 0.3f, 0.25f, 1.0f},
          "%s", bw::core::PortalTargetGraphDiagnosticText(
                    endpoint->targetGraphDiagnostic).data());
    }
    ImGui::TextColored(
        diagnostic == bw::core::PortalResolutionDiagnostic::None
            ? ImVec4{0.3f, 0.9f, 0.9f, 1.0f}
            : ImVec4{1.0f, 0.3f, 0.25f, 1.0f},
        "Aperture: %s",
        bw::core::PortalResolutionDiagnosticText(diagnostic).data());
    if (endpoint->resolved) {
      ImGui::Text(
          "Resolved width %.3f; walls %zu", endpoint->aperture.width,
          endpoint->aperture.wallIndices.size());
    }
  }

  if (!portalLoop && ImGui::Button("Delete Portal")) {
    transactUndoableActionAtomically(doc, CommandId::DeletePortal,
        [=](Document* document) { return deletePortal(document, layer, endpointId); });
  }
  if (portalLoop && ImGui::Button("Delete loop")) {
    transactUndoableActionAtomically(
        doc, CommandId::DeletePortalLoop,
        [=](Document* document) {
          return deletePortalLoop(document, layer, loopId);
        });
  }
}

}  // namespace editor
