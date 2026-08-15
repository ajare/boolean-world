#pragma once

#include <cstdint>
#include <vector>

#include <willpower/common/Timer.h>

#include "core/Platform.h"


namespace bw
{
	namespace core
	{
		struct PrimitiveProcessingStats
		{
			// Total number of input Primitives
			uint32_t candidateCount{ 0 };

			// Number of directly-visible (in cone) Primitives
			uint32_t visibleCount{ 0 };

			// Number of Primitives whose vertices were updated
			uint32_t updateVertexCount{ 0 };
		};

		struct ClipStats
		{
			// Number of input Primitives
			uint32_t primitivesProcessed{ 0 };

			// Total count of vertices in input Primitives
			uint32_t primVerticesProcessed{ 0 };

			// Number of Polygons generated (including holes)
			uint32_t polygonsGenerated{ 0 };

			// Total count ov vertices in generated Polygons
			uint32_t verticesGenerated{ 0 };

			// Number of vertices which were interpolated
			uint32_t interpolatedVertices{ 0 };

			// Number of duplicate polygons
			uint32_t duplicatePolygons{ 0 };

			// Number of duplicate tests
			uint32_t duplicateTests{ 0 };

			ClipStats operator+(ClipStats const& rhs) const
			{
				return {
					primitivesProcessed + rhs.primitivesProcessed,
					primVerticesProcessed + rhs.primVerticesProcessed,
					polygonsGenerated + rhs.polygonsGenerated,
					verticesGenerated + rhs.verticesGenerated,
					interpolatedVertices + rhs.interpolatedVertices,
					duplicatePolygons + rhs.duplicatePolygons,
					duplicateTests + rhs.duplicateTests
				};
			}

			ClipStats& operator+=(ClipStats const& rhs)
			{
				*this = this->operator+(rhs);
				return *this;
			}
		};

		struct TriangulationStats
		{
			uint32_t trianglesGenerated{ 0 };
		};

		struct Stats
		{
			PrimitiveProcessingStats prim;
			ClipStats clip;
			TriangulationStats tri;
		};

	} // core
} // bw