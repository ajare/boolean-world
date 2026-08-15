#pragma once

#include <vector>

#include "core/Platform.h"
#include "core/Vertex.h"


namespace bw
{
	namespace core
	{
		namespace graph
		{
			struct PolygonGraphVertex
			{
				float x, y;
				int64_t z;

				bool operator==(PolygonGraphVertex const& other) const
				{
					return x == other.x && y == other.y;
				}
			};

			struct PolygonGraphEdge
			{
				uint32_t v[2];
				uint32_t p[2]{ ~0u, ~0u };

				bool operator==(PolygonGraphEdge const& other) const
				{
					return (v[0] == other.v[0] && v[1] == other.v[1]) ||
						(v[0] == other.v[1] && v[1] == other.v[0]);
				}

				bool is2Sided() const
				{
					return p[0] != ~0u && p[1] != ~0u;
				}
			};

			struct PolygonGraphVertexHash
			{
				size_t operator()(PolygonGraphVertex const& pgv) const 
				{
					return std::hash<float>{}(pgv.x) ^ (std::hash<float>{}(pgv.y) << 1);
				}
			};

			struct PolygonGraphEdgeHash
			{
				size_t operator()(PolygonGraphEdge const& pge) const
				{
					auto v0 = (size_t)pge.v[0];
					auto v1 = (size_t)pge.v[1];

					if (v0 > v1)
					{
						std::swap(v0, v1);
					}

					return v0 + (v1 << 32);
				}
			};

			struct PolygonGraph
			{
				std::vector<PolygonGraphVertex> vertices;
				std::vector<PolygonGraphEdge> edges;

			public:

				void clear()
				{
					vertices.clear();
					edges.clear();
				}
			};

		} // graph
	} // core
} // bw