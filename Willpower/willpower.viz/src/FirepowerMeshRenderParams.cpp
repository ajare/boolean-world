#include "willpower/viz/FirepowerMeshRenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		using namespace std;
		using namespace wp;

		FirepowerMeshRenderParams::FirepowerMeshRenderParams(shared_ptr<mpp::ModelRenderParams> params)
			: RenderParams()
			, mParams(params)
			, mLineColour(mpp::Colour(1.0f, 1.0f, 0.0f))
		{
			mLineUniforms = make_shared<mpp::UniformCollection>();
			mLineUniforms->setUniform("DIFFUSE", glm::vec4(mLineColour.red, mLineColour.green, mLineColour.blue, mLineColour.alpha));
			mParams->setMeshUniforms("Lines", mLineUniforms);
		}

		void FirepowerMeshRenderParams::setLineColour(mpp::Colour const& colour)
		{
			mLineColour = colour;
			mLineUniforms->updateUniform("DIFFUSE", glm::vec4(mLineColour.red, mLineColour.green, mLineColour.blue, mLineColour.alpha));
		}

		mpp::Colour const& FirepowerMeshRenderParams::getLineColour() const
		{
			return mLineColour;
		}

		float FirepowerMeshRenderParams::getGridPadding() const
		{
			return 2;
		}

	} // viz
} // WP_NAMESPACE

