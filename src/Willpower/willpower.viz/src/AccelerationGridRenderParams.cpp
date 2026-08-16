#include "willpower/viz/AccelerationGridRenderParams.h"

namespace WP_NAMESPACE {
namespace viz {
using namespace std;
using namespace wp;

AccelerationGridRenderParams::AccelerationGridRenderParams(shared_ptr<mpp::ModelRenderParams> params)
    : RenderParams(), mParams(params), mLineColour(mpp::Colour(1.0f, 1.0f, 0.0f)) {
  mLineUniforms = make_shared<mpp::UniformCollection>();
  mLineUniforms->setUniform("DIFFUSE", glm::vec4(mLineColour.red, mLineColour.green, mLineColour.blue, mLineColour.alpha));
  mParams->setMeshUniforms("Lines", mLineUniforms);
}

void AccelerationGridRenderParams::setLineColour(mpp::Colour const& colour) {
  mLineColour = colour;
  mLineUniforms->updateUniform("DIFFUSE", glm::vec4(mLineColour.red, mLineColour.green, mLineColour.blue, mLineColour.alpha));
}

mpp::Colour const& AccelerationGridRenderParams::getLineColour() const {
  return mLineColour;
}

float AccelerationGridRenderParams::getGridPadding() const {
  return 2;
}

}  // namespace viz
}  // namespace WP_NAMESPACE
