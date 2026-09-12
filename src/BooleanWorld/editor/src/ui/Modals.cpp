#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void checkModalPopups(ViewContext& context) {
  auto* doc = context.doc;
  renderWorldLoadingDialog(doc);
  renderRecentWorldMissingDialog();

  auto& settings = context.settings;
  ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  auto& primitiveFieldPreview = getPrimitiveFieldPreview();
  constexpr char PrimitiveFieldPopupTitle[] = "Generate Primitive Field\u2026";

  if (primitiveFieldPreview.openRequested) {
    ImGui::OpenPopup(PrimitiveFieldPopupTitle);
    primitiveFieldPreview.openRequested = false;
  }

  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  bool keepPrimitiveFieldOpen = primitiveFieldPreview.open;
  if (ImGui::BeginPopupModal(
          PrimitiveFieldPopupTitle, &keepPrimitiveFieldOpen,
          ImGuiWindowFlags_AlwaysAutoResize)) {
    auto world = doc->getWorld();
    if (!world) {
      ImGui::TextUnformatted("The document world is no longer available.");
      if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
        primitiveFieldPreview.close();
      }
    } else {
      wp::Vector2 worldMinimum;
      wp::Vector2 worldMaximum;
      world->getExtents().getExtents(worldMinimum, worldMaximum);
      primitiveFieldPreview.poll(
          {worldMinimum, worldMaximum}, world->getNumPrimitives());

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Minimum site spacing", &primitiveFieldPreview.minimumSpacing,
              1.0f, 0.0f, 0.0f, "%.3f")) {
        primitiveFieldPreview.invalidateLayout();
      }

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputInt(
              "Requested batch maximum",
              &primitiveFieldPreview.maximumSites)) {
        primitiveFieldPreview.invalidateLayout();
      }

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputInt("Lloyd iterations",
                          &primitiveFieldPreview.lloydIterations)) {
        primitiveFieldPreview.invalidateLayout();
      }

      ImGui::TextUnformatted("Primitive types");
      auto typeCheckbox = [&](char const* label, bool& enabled) {
        if (ImGui::Checkbox(label, &enabled)) {
          primitiveFieldPreview.refreshPrimitives();
        }
      };
      typeCheckbox("Rectangle", primitiveFieldPreview.enabledTypes.rectangle);
      typeCheckbox("Triangle", primitiveFieldPreview.enabledTypes.triangle);
      typeCheckbox("Pentagon", primitiveFieldPreview.enabledTypes.pentagon);
      typeCheckbox("Hexagon", primitiveFieldPreview.enabledTypes.hexagon);
      typeCheckbox("Circle", primitiveFieldPreview.enabledTypes.circle);

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Cell occupancy (%)", &primitiveFieldPreview.occupancyPercent,
              0.25f, 0.0f, 100.0f, "%.2f")) {
        primitiveFieldPreview.refreshPrimitives();
      }
      ImGui::TextDisabled("The cell at 0, 0 is always occupied.");

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Hole chance (%)", &primitiveFieldPreview.holeChancePercent,
              0.25f, 0.0f, 100.0f, "%.2f")) {
        primitiveFieldPreview.refreshPrimitives();
      }
      ImGui::TextDisabled(
          "Occupied non-origin cells may receive a half-size Difference polygon.");

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Overlap (%)", &primitiveFieldPreview.overlapPercent, 0.25f,
              0.0f, 100.0f, "%.2f")) {
        primitiveFieldPreview.refreshPrimitives();
      }

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputInt("Seed", &primitiveFieldPreview.seed)) {
        primitiveFieldPreview.invalidateLayout();
      }
      ImGui::SameLine();
      if (ImGui::Button("Randomize")) {
        static std::random_device randomDevice;
        auto randomizedSeed = static_cast<int32_t>(randomDevice());
        if (randomizedSeed == primitiveFieldPreview.seed) {
          randomizedSeed = randomizedSeed == std::numeric_limits<int32_t>::max()
                               ? std::numeric_limits<int32_t>::min()
                               : randomizedSeed + 1;
        }
        primitiveFieldPreview.seed = randomizedSeed;
        primitiveFieldPreview.invalidateLayout();
      }

      auto controls = primitiveFieldPreview.evaluateControls(
          {worldMinimum, worldMaximum}, world->getNumPrimitives());
      auto generatedCount = primitiveFieldPreview.layout
                                ? primitiveFieldPreview.layout->sites.size()
                                : size_t{0};
      ImGui::Separator();
      if (controls.valid()) {
        ImGui::Text("Approximate uncapped sites: %llu",
                    static_cast<unsigned long long>(
                        controls.approximateUncappedSites));
      } else {
        ImGui::TextUnformatted("Approximate uncapped sites: unavailable");
      }
      ImGui::Text("Requested batch maximum: %d",
                  primitiveFieldPreview.maximumSites);
      ImGui::Text("Remaining world capacity: %u (authored primitives and ghost included)",
                  controls.remainingWorldCapacity);
      ImGui::Text("Effective placement cap: %u",
                  controls.effectivePlacementCap);
      ImGui::Text("Generated cells: %zu", generatedCount);
      auto holeCount = static_cast<size_t>(std::count_if(
          primitiveFieldPreview.primitives.begin(),
          primitiveFieldPreview.primitives.end(),
          [](PrimitiveFieldPrimitivePreview const& primitive) {
            return primitive.isHole;
          }));
      ImGui::Text("Occupied cells: %zu",
                  primitiveFieldPreview.primitives.size() - holeCount);
      ImGui::Text("Hole primitives: %zu", holeCount);
      ImGui::Text("Primitives to place: %zu",
                  primitiveFieldPreview.primitives.size());

      auto expensiveRequest = controls.approximateUncappedSites > 2000 ||
                              controls.effectivePlacementCap > 2000 ||
                              generatedCount > 2000;
      if (expensiveRequest) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.7f, 0.15f, 1.0f),
            "Performance warning: counts above 2,000 may take substantial time and memory.");
      }
      if (primitiveFieldPreview.layout &&
          primitiveFieldPreview.layout->samplingStoppedAtMaximum) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.7f, 0.15f, 1.0f),
            "Capped: center-outward sampling stopped at the effective placement cap.");
      }
      if (primitiveFieldPreview.primitives.size() >
          controls.remainingWorldCapacity) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "The occupied cells and holes require more primitive slots than remain.");
      }

      auto generationActive = primitiveFieldPreview.isGenerating();
      if (!controls.valid()) ImGui::BeginDisabled();
      if (ImGui::Button("Generate Layout")) {
        primitiveFieldPreview.generate(
            {worldMinimum, worldMaximum}, world->getNumPrimitives());
        generationActive = primitiveFieldPreview.isGenerating();
      }
      if (!controls.valid()) ImGui::EndDisabled();

      if (generationActive) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          primitiveFieldPreview.cancelGeneration();
          generationActive = false;
        }
      }

      ImGui::SameLine();
      auto canPlace = controls.valid() &&
                      primitiveFieldPreview.hasCompletePreview() &&
                      !primitiveFieldPreview.primitives.empty() &&
                      primitiveFieldPreview.primitives.size() <=
                          controls.remainingWorldCapacity &&
                      !generationActive;
      if (!canPlace) ImGui::BeginDisabled();
      if (ImGui::Button("Place Primitives") && primitiveFieldPreview.layout) {
        primitiveFieldPreview.beginPlacement();
        auto result = placePrimitiveField(
            doc, *primitiveFieldPreview.layout,
            primitiveFieldPreview.primitives, settings);
        primitiveFieldPreview.finishPlacement(result.placed, result.error);
        if (result.placed) {
          ImGui::CloseCurrentPopup();
          primitiveFieldPreview.close();
        }
      }
      if (!canPlace) ImGui::EndDisabled();

      if (generationActive) {
        auto phaseLabel = "Sampling sites";
        switch (primitiveFieldPreview.generationPhase()) {
          case bw::core::PrimitiveFieldLayoutPhase::Sampling:
            phaseLabel = "Sampling sites";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::LloydRelaxation:
            phaseLabel = "Lloyd relaxation";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::VoronoiConstruction:
            phaseLabel = "Constructing bounded Voronoi cells";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::Validation:
            phaseLabel = "Validating layout";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::Complete:
            phaseLabel = "Layout complete";
            break;
        }
        ImGui::ProgressBar(
            primitiveFieldPreview.generationProgress(), ImVec2(360.0f, 0.0f),
            phaseLabel);
      }

      char const* workflowStatus = "Idle — configure controls, then generate a layout.";
      switch (primitiveFieldPreview.state) {
        case PrimitiveFieldWorkflowState::Idle:
          break;
        case PrimitiveFieldWorkflowState::Generating:
          workflowStatus = "Generating layout…";
          break;
        case PrimitiveFieldWorkflowState::CurrentPreview:
          workflowStatus = primitiveFieldPreview.primitives.empty()
                               ? "Preview is current, but no cells were selected for placement."
                               : "Preview is current and ready to place.";
          break;
        case PrimitiveFieldWorkflowState::StalePreview:
          workflowStatus = "Preview is stale; generate the layout again.";
          break;
        case PrimitiveFieldWorkflowState::Cancelled:
          workflowStatus = "Generation was cancelled; document unchanged.";
          break;
        case PrimitiveFieldWorkflowState::Failed:
          workflowStatus = "Request failed; previous preview and document preserved.";
          break;
        case PrimitiveFieldWorkflowState::Placing:
          workflowStatus = "Placing primitives…";
          break;
        case PrimitiveFieldWorkflowState::NoCapacity:
          workflowStatus = "No capacity remains; delete primitives to continue.";
          break;
      }
      ImGui::TextWrapped("Status: %s", workflowStatus);

      if (!controls.valid() && primitiveFieldPreview.error.empty()) {
        ImGui::PushStyleColor(
            ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", controls.error.c_str());
        ImGui::PopStyleColor();
      }
      if (!primitiveFieldPreview.error.empty()) {
        ImGui::PushStyleColor(
            ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", primitiveFieldPreview.error.c_str());
        ImGui::PopStyleColor();
      }

      auto const* closeLabel = primitiveFieldPreview.layout ? "Close" : "Cancel";
      if (ImGui::Button(closeLabel, ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        primitiveFieldPreview.close();
      }
    }

    ImGui::EndPopup();
  }
  if (!keepPrimitiveFieldOpen && primitiveFieldPreview.open) {
    primitiveFieldPreview.close();
  }

  //
  // Help / instructions
  //
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(ED_WINDOW_WIDTH - 200, ED_WINDOW_HEIGHT - 200));

  if (ImGui::BeginPopupModal("Help", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    renderHelp();
    ImGui::EndPopup();
  }
}

}  // namespace editor
