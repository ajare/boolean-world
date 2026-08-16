#include "willpower/viz/CollisionSimulationRenderParams.h"

namespace WP_NAMESPACE {
namespace viz {
using namespace std;
using namespace wp;

CollisionSimulationRenderParams::CollisionSimulationRenderParams(shared_ptr<mpp::ModelRenderParams> params)
    : RenderParams(), mParams(params), mLineColour(mpp::Colour(1.0f, 1.0f, 0.0f)) {
  mLineUniforms = make_shared<mpp::UniformCollection>();
  mLineUniforms->setUniform("DIFFUSE", glm::vec4(mLineColour.red, mLineColour.green, mLineColour.blue, mLineColour.alpha));
  mParams->setMeshUniforms("Lines", mLineUniforms);
}

void CollisionSimulationRenderParams::setLineColour(mpp::Colour const& colour) {
  mLineColour = colour;
  mLineUniforms->updateUniform("DIFFUSE", glm::vec4(mLineColour.red, mLineColour.green, mLineColour.blue, mLineColour.alpha));
}

mpp::Colour const& CollisionSimulationRenderParams::getLineColour() const {
  return mLineColour;
}

float CollisionSimulationRenderParams::getGridPadding() const {
  return 2;
}

}  // namespace viz
}  // namespace WP_NAMESPACE
