#include "willpower/viz/DynamicQuadRenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		using namespace wp;
		using namespace std;

		DynamicQuadRenderParams::DynamicQuadRenderParams(shared_ptr<mpp::ModelRenderParams> params)
			: RenderParams()
		{
		}

		float DynamicQuadRenderParams::getGridPadding() const
		{
			return 0.0f;
		}

	} // viz
} // WP_NAMESPACE

