#include <sstream>

#include "willpower/serialization/SerializationUtils.h"

#include "willpower/geometry/SerializerMeshChunk.h"


namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace std;
		using namespace WP_NAMESPACE;
		using namespace serialization;

		SerializerMeshChunk::SerializerMeshChunk(string const& name, string const& description)
			: SerializerChunk(name, "Mesh", description)
		{
		}

		void SerializerMeshChunk::writeBinaryImpl(ostream& fp)
		{
			// Write vertices
			SerializationUtils::writeUint32(fp, (uint32_t)mVertices.size());

			for (uint32_t i = 0; i < mVertices.size(); ++i)
			{
				auto const& vertex = mVertices[i];

				SerializationUtils::writeVector2(fp, vertex.getPosition());
			}

			// Write edges
			SerializationUtils::writeUint32(fp, (uint32_t)mEdges.size());

			for (uint32_t i = 0; i < mEdges.size(); ++i)
			{
				auto const& edge = mEdges[i];

				SerializationUtils::writeUint32(fp, edge.getFirstVertex());
				SerializationUtils::writeUint32(fp, edge.getSecondVertex());
			}

			// Write polygons
			SerializationUtils::writeUint32(fp, (uint32_t)mPolygons.size());

			for (uint32_t i = 0; i < mPolygons.size(); ++i)
			{
				auto const& polygon = mPolygons[i];

				SerializationUtils::writeUint32(fp, polygon.getNumEdges());

				auto edgeIt = polygon.getFirstEdge();
				while (edgeIt != polygon.getEndEdge())
				{
					auto const& polygonEdge = *edgeIt;

					SerializationUtils::writeUint32(fp, polygonEdge.index);
					SerializationUtils::writeUint32(fp, polygonEdge.v0);
					SerializationUtils::writeUint32(fp, polygonEdge.v1);
						
					++edgeIt;
				}
			}
		}

		void SerializerMeshChunk::readBinaryImpl(istream& fp)
		{
			mVertices.clear();
			mEdges.clear();
			mPolygons.clear();

			// Read vertices
			uint32_t numVertices = SerializationUtils::readUint32(fp);

			for (uint32_t i = 0; i < numVertices; ++i)
			{
				Vector2 pos = SerializationUtils::readVector2(fp);

				auto v = Vertex(pos.x, pos.y);
				mVertices.push_back(v);
			}

			// Read edges
			uint32_t numEdges = SerializationUtils::readUint32(fp);

			for (uint32_t i = 0; i < numEdges; ++i)
			{
				uint32_t vertex0 = SerializationUtils::readUint32(fp);
				uint32_t vertex1 = SerializationUtils::readUint32(fp);

				auto e = Edge(vertex0, vertex1);
				mEdges.push_back(e);
			}

			// Read polygons
			uint32_t numPolygons = SerializationUtils::readUint32(fp);

			for (uint32_t i = 0; i < numPolygons; ++i)
			{

				uint32_t numPolygonEdges = SerializationUtils::readUint32(fp);

				IndexVector edgeData;
				for (uint32_t j = 0; j < numPolygonEdges; ++j)
				{
					uint32_t index = SerializationUtils::readUint32(fp);
					uint32_t v0 = SerializationUtils::readUint32(fp);
					uint32_t v1 = SerializationUtils::readUint32(fp);

					edgeData.push_back(v0);
					edgeData.push_back(v1);
					edgeData.push_back(index);
				}

				Polygon polygon(edgeData);
				mPolygons.push_back(polygon);
			}
		}

		void SerializerMeshChunk::writeTextImpl(ostream& fp)
		{
			// Write vertices
			fp << "# " << mVertices.size() << " vertices" << endl;
				
			for (uint32_t i = 0; i < mVertices.size(); ++i)
			{
				auto const& vertex = mVertices[i];
				fp << "Public id: " << vertex.getPublicId() << endl;
				fp << "Position: " << vertex.getPosition().x << ", " << vertex.getPosition().y << endl;
				fp << "Edgerefs:";
					
				auto const& edgeRefs = vertex.getEdgeReferences();
				for (uint32_t edgeRef : edgeRefs)
				{
					fp << " " << edgeRef << ",";
				}

				fp << endl;
			}

			fp << endl;
				
			// Write edges
			fp << "# " << mEdges.size() << " edge(s)" << endl;

			for (uint32_t i = 0; i < mEdges.size(); ++i)
			{
				auto const& edge = mEdges[i];
				fp << "Public id: " << edge.getPublicId() << endl;
				fp << "Vertices: " << edge.getFirstVertex() << ", " << edge.getSecondVertex() << endl;
				fp << "Polygonrefs:";

				auto const& polygonRefs = edge.getPolygonReferences();
				for (uint32_t polygonRef: polygonRefs)
				{
					fp << " " << polygonRef << ",";
				}

				fp << endl;
			}

			fp << endl;

			// Write polygons
			fp << "# " << mPolygons.size() << " polygon(s)" << endl;

			for (uint32_t i = 0; i < mPolygons.size(); ++i)
			{
				auto const& polygon = mPolygons[i];

				fp << "Public id: " << polygon.getPublicId() << endl;
				auto edgeIt = polygon.getFirstEdge();
				while (edgeIt != polygon.getEndEdge())
				{
					auto const& polygonEdge = *edgeIt;

					fp << "Edge: " << polygonEdge.index << " (" << polygonEdge.v0 << ", " << polygonEdge.v1 << ")" << endl;
					++edgeIt;
				}
			}
		}

		void SerializerMeshChunk::readTextImpl(istream& fp)
		{
			WP_UNUSED(fp);
		}

		void SerializerMeshChunk::addVertex(Vertex const& vertex)
		{
			mVertices.push_back(vertex);
		}

		void SerializerMeshChunk::addEdge(Edge const& edge)
		{
			mEdges.push_back(edge);
		}

		void SerializerMeshChunk::addPolygon(Polygon const& polygon)
		{
			mPolygons.push_back(polygon);
		}

		void SerializerMeshChunk::setMesh(Mesh* mesh)
		{
			mesh->compact();

			for (uint32_t i = 0; i < mesh->getNumVertices(); ++i)
			{
				addVertex(mesh->getVertex(i));
			}

			for (uint32_t i = 0; i < mesh->getNumEdges(); ++i)
			{
				addEdge(mesh->getEdge(i));
			}

			for (uint32_t i = 0; i < mesh->getNumPolygons(); ++i)
			{
				addPolygon(mesh->getPolygon(i));
			}
		}

		vector<Vertex> const& SerializerMeshChunk::getVertices() const
		{
			return mVertices;
		}

		vector<Edge> const& SerializerMeshChunk::getEdges() const
		{
			return mEdges;
		}

		vector<Polygon> const& SerializerMeshChunk::getPolygons() const
		{
			return mPolygons;
		}

	} // geometry
} // WP_NAMESPACE
