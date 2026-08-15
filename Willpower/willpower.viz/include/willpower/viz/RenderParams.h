#pragma once

#include "willpower/common/Platform.h"

#include "willpower/viz/Platform.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		
		class WP_VIZ_API RenderParams
		{
			bool mRender;

			bool mRenderAccelerationGrids;

		protected:

			bool mInvalidated;

		public:

			RenderParams();

			virtual ~RenderParams() = default;

			void setRender(bool render);

			bool getRender() const;

			bool isInvalidated() const;

			void rebuilt();

			void setRenderAccelerationGrids(bool render);

			bool getRenderAccelerationGrids() const;

			virtual float getGridPadding() const = 0;
		};

	} // viz
} // WP_NAMESPACE

