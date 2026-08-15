#pragma once

#include <mpp/Colour.h>
#include <mpp/ModelRenderParams.h>

#include "willpower/viz/Platform.h"
#include "willpower/viz/RenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		class WP_VIZ_API DynamicTriangleRenderParams : public RenderParams
		{
		public:

			explicit DynamicTriangleRenderParams(std::shared_ptr<mpp::ModelRenderParams> params);

			float getGridPadding() const override;
		};

	} // viz
} // WP_NAMESPACE

