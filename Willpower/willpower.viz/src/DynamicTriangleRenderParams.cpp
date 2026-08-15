#include "willpower/viz/DynamicTriangleRenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		using namespace wp;
		using namespace std;

		DynamicTriangleRenderParams::DynamicTriangleRenderParams(shared_ptr<mpp::ModelRenderParams> params)
			: RenderParams()
		{
		}

		float DynamicTriangleRenderParams::getGridPadding() const
		{
			return 0.0f;
		}

	} // viz
} // WP_NAMESPACE

