#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderPortalsView(ViewContext& context) {
  auto* doc = context.doc;
  auto* layer = doc->getWorld()->getActiveLayer();

  if (ImGui::Button("Create Mirror Portal")) {
    transactUndoableActionAtomically(doc, CommandId::CreatePortal,
        [=](Document* document) { return createPortal(document, layer); });
  }
  ImGui::SameLine();
  widgets::HelpMarker(
      "Creates a named Portal on this Layer. New Portals target themselves and "
      "therefore begin as mirrors; choose another same-Layer Portal as the "
      "target to build a directed cycle.");
  for (auto const& portal : layer->getPortals()) {
    auto label = format("{}{}###portal{}", portal.getName(),
        portal.getTargetId() == portal.getId() ? " — Mirror" : "", portal.getId());
    if (ImGui::Selectable(label.c_str(),
            doc->getSelectedPortalLayerId() == layer->getId() &&
            doc->getSelectedPortalId() == portal.getId())) {
      transactUndoableAction(doc, CommandId::SelectPortal,
          [layerId = layer->getId(), id = portal.getId()](Document* document) {
            return selectPortal(document, layerId,
                id);
          });
    }
  }

  if (doc->getSelectedPortalLayerId() != layer->getId()) return;
  auto const portalId = doc->getSelectedPortalId();
  auto const* portal = layer->getPortal(portalId);
  auto const* authored = portal ? &portal->getAperture() : nullptr;
  if (!authored) return;

  ImGui::SeparatorText("Selected Portal");
  if (portal->getTargetId() == portal->getId()) {
    auto cyberspace = portal->getCyberspace();
    if (ImGui::Checkbox("Cyberspace", &cyberspace)) {
      transactUndoableActionAtomically(doc, CommandId::SetPortalCyberspace,
          [=](Document* document) {
            return setPortalCyberspace(document, layer, portalId, cyberspace);
          });
    }
  }
  auto blocksWater = portal->getBlocksWater();
  if (ImGui::Checkbox("Blocks water", &blocksWater)) {
    transactUndoableActionAtomically(doc, CommandId::SetPortalBlocksWater,
        [=](Document* document) {
          return setPortalBlocksWater(document, layer, portalId, blocksWater);
        });
    // Transactions may replace the document snapshot and invalidate pointers.
    return;
  }
  ImGui::SameLine();
  widgets::HelpMarker(
      "Blocks all Liquid leaving this Portal, but allows Liquid arriving from "
      "another Portal. Mirrors never transport Liquid. Changes regenerate "
      "Liquid from authored inputs.");
  {
    auto const* portal = layer->getPortal(portalId);
    auto name = portal->getName();
    static string renameError;
    static string renameErrorKey;
    auto const errorKey = format("{}:{}:{}:{}", static_cast<void*>(doc),
                                 layer->getId(), portalId, name);
    if (renameErrorKey != errorKey) renameError.clear();
    renameErrorKey = errorKey;
    if (!renameError.empty()) ImGui::TextWrapped("%s", renameError.c_str());
    ImGui::PushID(static_cast<int>(layer->getId()));
    ImGui::PushID(static_cast<int>(portalId));
    bool const renamed = widgets::InputText(
        "Name##Portal", &name, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopID();
    ImGui::PopID();
    if (renamed) {
      renameError.clear();
      try {
        transactUndoableActionAtomically(doc, CommandId::SetPortalName,
            [=](Document* document) {
              return setPortalName(document, layer, portalId, name);
            });
      } catch (std::exception const& error) {
        renameError = error.what();
      }
      // A rejected/no-op transaction restores the document snapshot as well.
      // Do not retain pointers into the previous Layer for the rest of this frame.
      return;
    }
    auto targetLabel = [&](bw::core::Portal const& target) {
      return target.getName() + (target.getId() == portalId ? " (self — Mirror)" : "");
    };
    auto preview = targetLabel(*layer->getPortal(portal->getTargetId()));
    if (ImGui::BeginCombo("Target", preview.c_str())) {
      for (auto const& target : layer->getPortals()) {
        auto label = targetLabel(target);
        if (ImGui::Selectable(label.c_str(), target.getId() == portal->getTargetId()) &&
            target.getId() != portal->getTargetId()) {
          auto targetId = target.getId();
          transactUndoableActionAtomically(doc, CommandId::SetPortalTarget,
              [=](Document* document) {
                return setPortalTarget(document, layer, portalId, targetId);
              });
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    widgets::HelpMarker(
        "Targets are stable Portal identities on this Layer, not names. A self "
        "target is a Mirror Portal. Multi-Portal components operate only after "
        "every Portal has exactly one incoming target and the targets form one "
        "closed cycle.");
  }
  auto const aperture = *authored;
  float centre[2]{aperture.centre.x, aperture.centre.y};
  if (ImGui::InputFloat2("Centre", centre)) {
    auto position = wp::Vector2{centre[0], centre[1]};
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalPosition,
        [=](Document* document) {
          return setPortalPosition(
              document, layer, portalId, position);
        });
  }
  auto width = aperture.width;
  if (ImGui::InputFloat("Width", &width) && width > 0.0f) {
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalWidth,
        [=](Document* document) {
          return setPortalWidth(
              document, layer, portalId, width);
        });
  }
  float vertical[2]{aperture.bottom, aperture.top};
  if (ImGui::InputFloat2("Bottom / top", vertical) &&
      vertical[1] > vertical[0]) {
    transactUndoableActionAtomically(
        doc, CommandId::SetPortalVerticalBounds,
        [=](Document* document) {
          return setPortalVerticalBounds(
              document, layer, portalId, vertical[0],
              vertical[1]);
        });
  }

  auto const* resolved = context.worldData
                             ? context.worldData->findPortalLoop(
                                   layer->getId(), portalId)
                             : nullptr;
  if (!resolved) {
    ImGui::TextDisabled("Inactive or awaiting generation.");
  } else {
    auto const* endpoint = context.worldData->findPortalEndpoint(
        layer->getId(), portalId);
    if (!endpoint) {
      ImGui::TextDisabled("Waiting for Portal generation.");
      return;
    }
    auto const diagnostic = endpoint->diagnostic;
    {
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

  if (ImGui::Button("Delete Portal")) {
    transactUndoableActionAtomically(doc, CommandId::DeletePortal,
        [=](Document* document) { return deletePortal(document, layer, portalId); });
  }

}

}  // namespace editor
