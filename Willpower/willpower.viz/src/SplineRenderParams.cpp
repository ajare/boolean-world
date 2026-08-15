#include "willpower/viz/SplineRenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		using namespace std;
		using namespace wp;

		SplineRenderParams::SplineRenderParams()
			: RenderParams()
			, mSplineWidth(1)
			, mSplineColour(mpp::Colour(0.7f, 0.7f, 0.7f))
			, mRenderControlPoints(false)
			, mPointSize(6.0f)
			, mPointColour(mpp::Colour(1.0f, 1.0f, 1.0f))
		{
		}

		void SplineRenderParams::setSplineWidth(float width)
		{
			mSplineWidth = width;
			mInvalidated = true;
		}

		float SplineRenderParams::getSplineWidth() const
		{
			return mSplineWidth;
		}

		void SplineRenderParams::setSplineColour(mpp::Colour const& colour)
		{
			mSplineColour = colour;
		}

		mpp::Colour const& SplineRenderParams::getSplineColour() const
		{
			return mSplineColour;
		}

		void SplineRenderParams::setRenderControlPoints(bool render)
		{
			mRenderControlPoints = render;
		}

		bool SplineRenderParams::getRenderControlPoints() const
		{
			return mRenderControlPoints;
		}

		float SplineRenderParams::getPointSize() const
		{
			return mPointSize;
		}

		void SplineRenderParams::setPointColour(mpp::Colour const& colour)
		{
			mPointColour = colour;
		}

		mpp::Colour const& SplineRenderParams::getPointColour() const
		{
			return mPointColour;
		}

		float SplineRenderParams::getGridPadding() const
		{
			return 2;
		}

	} // viz
} // WP_NAMESPACE

