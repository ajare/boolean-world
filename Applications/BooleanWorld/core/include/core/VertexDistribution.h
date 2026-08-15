#pragma once

#include <vector>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include "core/Platform.h"

namespace bw
{
	namespace core
	{

		class BW_API VertexDistribution
		{
			std::vector<wp::Vector2> mPoints;

		public:

			VertexDistribution();

			virtual ~VertexDistribution() = default;

			void generatePoissonDiskFlat(wp::BoundingBox const& area, float r, float z);
		};

	} // core
} // bw