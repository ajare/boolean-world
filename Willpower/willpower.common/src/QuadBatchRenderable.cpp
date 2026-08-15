#include <algorithm>

#include "willpower/common/QuadBatchRenderable.h"

using namespace std;

namespace WP_NAMESPACE
{

	QuadBatchRenderable::QuadBatchRenderable(int count, int posStride, int texStride, int colourOffset, bool renderAsTriangles)
		: BatchRenderable(count, posStride, texStride, colourOffset)
		, mRenderAsTriangles(renderAsTriangles)
		, mTextureRatio(0.0f)
	{
	}

	int8_t* QuadBatchRenderable::getColourData(int vertexIndex)
	{
		int vertexStride = mPosStride;
		if (mRenderAsTriangles)
		{
			vertexIndex <<= 2;
		}

		return &(mPositionData[vertexStride * vertexIndex + mColourOffset]);
	}

	void QuadBatchRenderable::setTextureRatio(float ratio)
	{
		mTextureRatio = ratio;
	}

	float QuadBatchRenderable::getTextureRatio() const
	{
		return mTextureRatio;
	}

	bool QuadBatchRenderable::renderAsTriangles() const
	{
		return mRenderAsTriangles;
	}

} // WP_NAMESPACE
