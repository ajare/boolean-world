#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderDebug(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto const* worldData = context.worldData;
  auto globalTime = context.globalTime;
  auto windowFlags = 0;

  if (ImGui::Begin("Debug")) {
    if (ImGui::CollapsingHeader("Transform", nullptr, windowFlags)) {
      auto const& proxyPos = doc->getPlayerProxyPosition();
      auto proxyAngle = doc->getPlayerProxyAngle();

      ImGui::Text("Proxy position: %3.2f, %3.2f", proxyPos.x, proxyPos.y);
      ImGui::Text("Proxy angle: %3.2f", proxyAngle);

      if (doc->isActive()) {
        auto const& selection = doc->getSelectedPrimitiveIndices();

        if (selection.size() == 1) {
          auto world = doc->getWorld();

          auto primitive = world->getPrimitive(*selection.begin());
          auto const& influencePos = primitive->getInfluenceEyeOriginPosition();

          ImGui::Text("Eye position: %3.2f, %3.2f", influencePos.x, influencePos.y);

          auto const& inputs = primitive->getInputs();

          ImGui::Text("Influence dist: %3.2f", inputs.entityInfluenceDistance);
          ImGui::Text("Influence angle: %3.2f", inputs.entityInfluenceAngle);
          ImGui::Text("Entity angle: %3.2f", inputs.entityGlobalAngle);

          auto scaleOut = primitive->transformT(bw::core::VertexTransformer::Key::Scale, globalTime);
          auto angleOut = primitive->transformT(bw::core::VertexTransformer::Key::Angle, globalTime);
          auto orbitAngleOut = primitive->transformT(bw::core::VertexTransformer::Key::OrbitAngle, globalTime);
          auto orbitDistOut = primitive->transformT(bw::core::VertexTransformer::Key::OrbitDistance, globalTime);

          ImGui::Text("Trans scale out: %3.2f", scaleOut);
          ImGui::Text("Trans angle out: %3.2f", angleOut);
          ImGui::Text("Trans orbit angle out: %3.2f", orbitAngleOut);
          ImGui::Text("Trans orbit dist out: %3.2f", orbitDistOut);

          auto scaleAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::Scale).getValue(scaleOut);
          auto angleAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::Angle).getValue(angleOut);
          auto orbitAngleAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::OrbitAngle).getValue(orbitAngleOut);
          auto orbitDistAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::OrbitDistance).getValue(orbitDistOut);

          ImGui::Text("Anim scale: %3.2f", scaleAnim);
          ImGui::Text("Anim angle: %3.2f", angleAnim);
          ImGui::Text("Anim orbit angle: %3.2f", orbitAngleAnim);
          ImGui::Text("Anim orbit dist: %3.2f", orbitDistAnim);

          float infl = BW_INTERPOLATOR_MAX_DISTANCE - primitive->getInputs().entityInfluenceDistance;

          auto scaleInfl = scaleAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::Scale).getValue(infl);
          auto angleInfl = angleAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::Angle).getValue(infl);
          auto orbitAngleInfl = orbitAngleAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::OrbitAngle).getValue(infl);
          auto orbitDistInfl = orbitDistAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::OrbitDistance).getValue(infl);

          ImGui::Text("Influenced scale: %3.2f", scaleInfl);
          ImGui::Text("Influenced angle: %3.2f", angleInfl);
          ImGui::Text("Influenced orbit angle: %3.2f", orbitAngleInfl);
          ImGui::Text("Influenced orbit dist: %3.2f", orbitDistInfl);
        }
      }
    }

    if (doc->isActive() && worldData) {
      if (ImGui::CollapsingHeader("Arrangement face", nullptr, windowFlags)) {
        ImGui::Text(
            "Total vertices: %u",
            uint32_t(worldData->getArrangement().vertices.size()));
        renderArrangementFaceView(context);
      }
    }
  }

  ImGui::End();
}


}  // namespace editor
