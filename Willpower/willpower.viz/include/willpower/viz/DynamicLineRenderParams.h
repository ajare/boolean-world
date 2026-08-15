#pragma once

#include <mpp/Colour.h>
#include <mpp/ModelRenderParams.h>

#include "willpower/viz/Platform.h"
#include "willpower/viz/RenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		class DynamicLineRenderer;

		class WP_VIZ_API DynamicLineRenderParams : public RenderParams
		{
			friend class DynamicLineRenderer;

		protected:

			explicit DynamicLineRenderParams(std::shared_ptr<mpp::ModelRenderParams> params);

		public:

			float getGridPadding() const override;
		};

	} // viz
} // WP_NAMESPACE

