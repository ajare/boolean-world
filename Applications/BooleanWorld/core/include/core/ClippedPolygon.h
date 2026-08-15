#pragma once

#include <vector>

#include <willpower/common/BoundingBox.h>

#include "core/Vertex.h"
#include "core/Edge.h"


namespace bw
{
	namespace core
	{

		struct ClippedPolygon
		{
			bool isHole;
			ClosedPolygon vertices;
			uint32_t primitiveIndex;
			wp::BoundingBox bounds;
		};

	} // core
} // bw