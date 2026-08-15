#pragma once

#include <mpp/Colour.h>
#include <mpp/ModelRenderParams.h>

#include "willpower/viz/Platform.h"
#include "willpower/viz/RenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{

		class WP_VIZ_API FirepowerMeshRenderParams : public RenderParams
		{
			std::shared_ptr<mpp::ModelRenderParams> mParams;

			std::shared_ptr<mpp::UniformCollection> mLineUniforms;

			mpp::Colour mLineColour;

		public:

			explicit FirepowerMeshRenderParams(std::shared_ptr<mpp::ModelRenderParams> params);

			void setLineColour(mpp::Colour const& colour);

			mpp::Colour const& getLineColour() const;

			float getGridPadding() const;

		};

	} // viz
} // WP_NAMESPACE

