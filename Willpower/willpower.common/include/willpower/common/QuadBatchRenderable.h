#pragma once

#include <vector>

#include <mpp/mesh/Vertex.h>

#include "willpower/common/BatchRenderable.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API QuadBatchRenderable : public BatchRenderable
	{
		float mTextureRatio;

		bool mRenderAsTriangles;

	public:

		QuadBatchRenderable(int count, int posStride, int texStride, int colourOffset, bool renderAsTriangles);
		
		int8_t* getColourData(int index = 0);

		void setTextureRatio(float ratio);

		float getTextureRatio() const;

		bool renderAsTriangles() const;

	};

} // WP_NAMESPACE
