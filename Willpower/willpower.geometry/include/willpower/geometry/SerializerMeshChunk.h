#pragma once

#include <map>
#include <functional>
#include <vector>



#include "willpower/serialization/SerializerChunk.h"

#include "willpower/geometry/Platform.h"
#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/Vertex.h"
#include "willpower/geometry/Edge.h"
#include "willpower/geometry/Polygon.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		class WP_GEOMETRY_API SerializerMeshChunk : public serialization::SerializerChunk
		{

			std::vector<Vertex> mVertices;

			std::vector<Edge> mEdges;

			std::vector<Polygon> mPolygons;

		private:

			void writeBinaryImpl(std::ostream& fp);

			void readBinaryImpl(std::istream& fp);

			void writeTextImpl(std::ostream& fp);

			void readTextImpl(std::istream& fp);
				
			void addVertex(Vertex const& vertex);

			void addEdge(Edge const& edge);

			void addPolygon(Polygon const& polygon);

		public:

			SerializerMeshChunk(std::string const& name, std::string const& description);

			void setMesh(Mesh* mesh);

			std::vector<Vertex> const& getVertices() const;

			std::vector<Edge> const& getEdges() const;

			std::vector<Polygon> const& getPolygons() const;
		};

		} // geometry
} // WP_NAMESPACE
