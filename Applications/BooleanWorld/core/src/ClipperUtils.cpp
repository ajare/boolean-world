#include <ranges>

#include "core/Defines.h"
#include "core/ClipperDefines.h"
#include "core/ClipperUtils.h"


namespace bw
{
	namespace core
	{
		using namespace std;

		struct PointHash
		{
			size_t operator()(const Clipper2Lib::Point64& p) const noexcept
			{
				return std::hash<int64_t>{}(p.x) ^
					(std::hash<int64_t>{}(p.y) << 1);
			}
		};

		struct PointEq
		{
			bool operator()(const Clipper2Lib::Point64& a,
				const Clipper2Lib::Point64& b) const noexcept
			{
				return a.x == b.x && a.y == b.y;
			}
		};

		struct PathEdge
		{
			int a;
			int b;
			bool used = false;
		};

		Clipper2Lib::Paths64 SplitTouchingPolygon(const Clipper2Lib::Path64& path)
		{
			Clipper2Lib::Paths64 result;

			if (path.size() < 3)
				return result;

			// ------------------------------------
			// Build vertex table
			// ------------------------------------

			std::unordered_map<Clipper2Lib::Point64, int, PointHash, PointEq> id_of;
			std::vector<Clipper2Lib::Point64> vertices;

			auto GetVertexId = [&](const Clipper2Lib::Point64& p)
			{
				auto it = id_of.find(p);

				if (it != id_of.end())
					return it->second;

				int id = (int)vertices.size();

				vertices.push_back(p);
				id_of.emplace(p, id);

				return id;
			};

			// ------------------------------------
			// Build graph
			// ------------------------------------

			std::vector<PathEdge> edges;
			std::vector<std::vector<int>> adjacency;

			for (const auto& p : path)
				GetVertexId(p);

			adjacency.resize(vertices.size());

			auto AddEdge = [&](int a, int b)
			{
				int idx = (int)edges.size();

				edges.push_back({ a, b, false });

				adjacency[a].push_back(idx);
				adjacency[b].push_back(idx);
			};

			for (size_t i = 0; i < path.size(); ++i)
			{
				int a = GetVertexId(path[i]);
				int b = GetVertexId(path[(i + 1) % path.size()]);

				AddEdge(a, b);
			}

			// ------------------------------------
			// Extract loops
			// ------------------------------------

			auto Other = [&](int edgeIdx, int v)
			{
				const PathEdge& e = edges[edgeIdx];
				return e.a == v ? e.b : e.a;
			};

			while (true)
			{
				int startEdge = -1;

				for (int i = 0; i < (int)edges.size(); ++i)
				{
					if (!edges[i].used)
					{
						startEdge = i;
						break;
					}
				}

				if (startEdge < 0)
					break;

				Clipper2Lib::Path64 poly;

				int startVertex = edges[startEdge].a;
				int currentVertex = startVertex;
				int currentEdge = startEdge;

				poly.push_back(vertices[startVertex]);

				while (true)
				{
					edges[currentEdge].used = true;

					currentVertex =
						Other(currentEdge, currentVertex);

					poly.push_back(vertices[currentVertex]);

					if (currentVertex == startVertex)
						break;

					int nextEdge = -1;

					for (int e : adjacency[currentVertex])
					{
						if (!edges[e].used)
						{
							nextEdge = e;
							break;
						}
					}

					if (nextEdge < 0)
						break;

					currentEdge = nextEdge;
				}

				if (poly.size() >= 4)
				{
					if (Area(poly) < 0)
						std::reverse(poly.begin(), poly.end());

					result.push_back(std::move(poly));
				}
			}

			return result;
		}

		size_t booth(Clipper2Lib::Path64 const& path)
		{
			size_t n = path.size();
			Clipper2Lib::Path64 path2(path);
			
			path2.insert(path2.end(), path.begin(), path.end());

			size_t i = 0, j = 1, k = 0;

			while (i < n && j < n && k < n)
			{
				if (path2[i + k].x == path2[j + k].x && path2[i + k].y == path2[j + k].y)
				{
					++k;
					continue;
				}

				if ((path2[i + k].x > path2[j + k].x) || (path2[i + k].y > path2[j + k].y))
				{
					i += k + 1;
				}
				else
				{
					j += k + 1;
				}

				if (i == j)
				{
					++j;
				}

				k = 0;
			}

			return min(i, j);
		}

		void interpolateVertex(int64_t prevZ, int64_t* z, int64_t nextZ, vector<WorldVertexData>& vertexData)
		{
			// We only want to set properties if this vertex is in an interpolated/intersection
			if (!BW_VERTEX_Z_IS_INTERPOLATED(*z))
			{
				return;
			}

			auto cIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(*z);
			auto& cData = vertexData[cIndex];

			auto pIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(prevZ);
			auto nIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(nextZ);

			auto nextSet = BW_VERTEX_Z_GET_PREV_PROP(nextZ);

			if (!BW_VERTEX_Z_GET_PREV_PROP(*z))
			{
				auto const& pData = vertexData[pIndex];
				cData.properties[0] = pData.properties[1];

				*z = BW_VERTEX_Z_SET_PREV_PROP(*z, 1);
			}

			if (!BW_VERTEX_Z_GET_NEXT_PROP(*z))
			{
				// If next is not set, then use cData 0
				auto const& nData = vertexData[nIndex];
				cData.properties[1] = nextSet ? nData.properties[0] : cData.properties[0];

				*z = BW_VERTEX_Z_SET_NEXT_PROP(*z, 1);
			}
 		}

		void setVertex(int64_t* z, PrimitivePropertySet const& props, vector<WorldVertexData>& vertexData)
		{
			auto cIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(*z);
			auto& cData = vertexData[cIndex];
			
			cData.properties[0] = props;
			cData.properties[1] = props;

			*z = BW_VERTEX_Z_SET_PREV_PROP(*z, 1);
			*z = BW_VERTEX_Z_SET_NEXT_PROP(*z, 1);
		}

		void ClipperUtils::interpolatePathVertices(Clipper2Lib::Path64& path, PrimitivePropertySet const& clipProperties, vector<WorldVertexData>& vertexData)
		{
			int j = 0, numPoints = (int)path.size();

			// Find the first non-intersection vertex, or intersection vertex which has both of its properties set.
			while (j < numPoints)
			{
				auto z = path[j].z;

				auto prevSet = BW_VERTEX_Z_GET_NEXT_PROP(z);
				auto nextSet = BW_VERTEX_Z_GET_PREV_PROP(z);

				if (prevSet + nextSet == 2)
				{
					break;
				}

				j++;
			}

			if (j == numPoints)
			{
				// Polygon is entirely interpolated
				for (auto& point : path)
				{
					setVertex(&point.z, clipProperties, vertexData);
				}
			}
			else
			{
				auto j1 = j;

				while (j < numPoints)
				{
					int i = j == 0 ? numPoints - 1 : j - 1;
					int k = j == numPoints - 1 ? 0 : j + 1;

					interpolateVertex(path[i].z, &path[j].z, path[k].z, vertexData);
					j++;
				}

				j = 0;

				while (j < j1)
				{
					int i = j == 0 ? numPoints - 1 : j - 1;
					int k = j == numPoints - 1 ? 0 : j + 1;

					interpolateVertex(path[i].z, &path[j].z, path[k].z, vertexData);
					j++;
				}
			}
		}

		void addTraversedPath(Clipper2Lib::Path64 const& polygon, bool isHole, vector<Clipper2Polygon>& paths, PrimitivePropertySet const& clipProperties, vector<WorldVertexData>& vertexData)
		{
			paths.push_back({ isHole, ~0u, polygon });
			ClipperUtils::interpolatePathVertices(paths.back().path, clipProperties, vertexData);
		}

		void ClipperUtils::traverseNonHole(Clipper2Lib::PolyPath64 const* polyPath, vector<Clipper2Polygon>& outPaths, bool interpolateVertices, PrimitivePropertySet const& clipProperties, vector<WorldVertexData>& vertexData)
		{
			// Traverse child holes breadth-first
			for (auto it = polyPath->begin(); it != polyPath->end(); ++it)
			{
				addTraversedPath(it->get()->Polygon(), true, outPaths, clipProperties, vertexData);
			}

			for (auto it = polyPath->begin(); it != polyPath->end(); ++it)
			{
				traverseHole(it->get(), outPaths, interpolateVertices, clipProperties, vertexData);
			}
		}

		void ClipperUtils::traverseHole(Clipper2Lib::PolyPath64 const* polyPath, vector<Clipper2Polygon>& outPaths, bool interpolateVertices, PrimitivePropertySet const& clipProperties, vector<WorldVertexData>& vertexData)
		{
			// Traverse child non-holes depth-first
			for (auto it = polyPath->begin(); it != polyPath->end(); ++it)
			{
				auto const* c = it->get();

				addTraversedPath(c->Polygon(), false, outPaths, clipProperties, vertexData);
				traverseNonHole(c, outPaths, interpolateVertices, clipProperties, vertexData);
			}
		}

		void ClipperUtils::traverseTree(Clipper2Lib::PolyTree64 const* polyTree, vector<Clipper2Polygon>& outPaths, bool interpolateVertices, PrimitivePropertySet const& clipProperties, vector<WorldVertexData>& vertexData)
		{
			// Traverse child non-holes depth-first
			for (auto it = polyTree->begin(); it != polyTree->end(); ++it)
			{
				auto const* c = it->get();

				addTraversedPath(c->Polygon(), false, outPaths, clipProperties, vertexData);
				traverseNonHole(c, outPaths, interpolateVertices, clipProperties, vertexData);
			}
		}

		vector<ClippedPolygon> ClipperUtils::convertClipper2PolygonsToClippedPolygons(vector<Clipper2Polygon> const& polygons, uint32_t* numVerticesGenerated)
		{
			vector<ClippedPolygon> result;

			if (numVerticesGenerated)
			{
				*numVerticesGenerated = 0;
			}

			for (auto const& polygon : polygons)
			{
				ClosedPolygon list;

				if (numVerticesGenerated)
				{
					*numVerticesGenerated += (uint32_t)polygon.path.size();
				}

				wp::Vector2 boundsMin{ 1e10f, 1e10f }, boundsMax{ -1e10f, -1e10f };
				for (auto const& point : polygon.path)
				{
					auto x = (float)(static_cast<float>(point.x) / BW_CLIPPER_SCALE);
					auto y = (float)(static_cast<float>(point.y) / BW_CLIPPER_SCALE);

					if (x > boundsMax.x)
					{
						boundsMax.x = x;
					}
					if (y > boundsMax.y)
					{
						boundsMax.y = y;
					}
					if (x < boundsMin.x)
					{
						boundsMin.x = x;
					}
					if (y < boundsMin.y)
					{
						boundsMin.y = y;
					}

					list.push_back({ wp::Vector2(x, y), point.z });
				}

				result.push_back({
					polygon.isHole,
					list,
					polygon.primitiveIndex,
					wp::BoundingBox(boundsMin, boundsMax - boundsMin)
				});
			}

			return result;
		}

		vector<Clipper2Polygon> ClipperUtils::convertPathsToClippedPolygons(Clipper2Lib::Paths64 const& path)
		{
			vector<Clipper2Polygon> polygons;

			for (auto const& p : path)
			{
				polygons.push_back({
					Clipper2Lib::IsPositive(p),
					~0u,
					p
				});
			}

			return polygons;
		}

		vector<Clipper2Polygon> ClipperUtils::convertPrimitiveToClipperPolygons(Primitive const* primitive)
		{
			auto paths = ClipperUtils::convertComplexPolygonsToPath(primitive);
			auto primitiveIndex = (uint32_t)(BW_VERTEX_Z_UNPACK_PRIMITIVE_INDEX(paths[0][0].z));
			auto operation = primitive->getOperation();

			vector<Clipper2Polygon> polygons;

			// We treat the path as a hole if it is either not the first path in the list, or has a Difference operation.
			for (uint32_t i = 0; i < (uint32_t)paths.size(); ++i)
			{
				polygons.push_back({ i != 0 || operation == Primitive::Operation::Difference, primitiveIndex, paths[i]});
			}

			return polygons;
		}

		Clipper2Lib::Paths64 ClipperUtils::convertComplexPolygonsToPath(vector<ComplexPolygon> const& complexPolygons)
		{
			Clipper2Lib::Paths64 paths;

			for (auto const& complexPolygon : complexPolygons)
			{
				for (auto const& polygon : complexPolygon)
				{
					auto path = vector<Clipper2Lib::Point64>();

					for (auto const& vertex : polygon)
					{
						path.push_back(BW_CLIPPER_MAKE_POINT((double)vertex.p.x, (double)vertex.p.y, vertex.z));
					}

					paths.push_back(path);
				}
			}

			return paths;
		}

		Clipper2Lib::Paths64 ClipperUtils::convertClippedPolygonsToPath(vector<ClippedPolygon> const& polygons)
		{
			Clipper2Lib::Paths64 paths;

			for (auto const& polygon : polygons)
			{
				auto path = vector<Clipper2Lib::Point64>();

				for (auto const& vertex : polygon.vertices)
				{
					path.push_back(BW_CLIPPER_MAKE_POINT((double)vertex.p.x, (double)vertex.p.y, vertex.z));
				}

				paths.push_back(path);
			}

			return paths;
		}

		Clipper2Lib::Paths64 ClipperUtils::convertClipper2PolygonsToPath(vector<Clipper2Polygon> const& polygons)
		{
			Clipper2Lib::Paths64 paths;

			for (auto const& polygon : polygons)
			{
				paths.push_back(polygon.path);
			}

			return paths;
		}

		Clipper2Lib::Paths64 ClipperUtils::convertComplexPolygonsToPath(Primitive const* primitive)
		{
			return convertComplexPolygonsToPath(primitive->getVertices());
		}

		vector<ComplexPolygon> ClipperUtils::convertClippedToComplexPolygons(vector<ClippedPolygon> const& clippedPolygons, wp::BoundingBox* bounds)
		{
			auto numPolygons = (uint32_t)clippedPolygons.size();

			if (numPolygons == 0)
			{
				return {};
			}

			vector<ComplexPolygon> complexPolygons;

			ComplexPolygon complexPolygon = { clippedPolygons[0].vertices };

			if (bounds)
			{
				*bounds = calculatePolygonBounds(complexPolygon[0]);
			}

			for (uint32_t i = 1; i < numPolygons; ++i)
			{
				auto const& clippedPolygon = clippedPolygons[i];

				if (clippedPolygon.isHole)
				{
					complexPolygon.push_back(clippedPolygon.vertices);
				}
				else
				{
					complexPolygons.push_back(complexPolygon);
					complexPolygon = { clippedPolygon.vertices };

					// Calculate extents: non-holes are guaranteed to have larger extents
					// so we than holes so we only need to do these
					if (bounds)
					{
						*bounds = bounds->unionWith(calculatePolygonBounds(complexPolygon[0]));
					}
				}
			}

			complexPolygons.push_back(complexPolygon);

			return complexPolygons;
		}

		bool ClipperUtils::arePolygonsRotation(Clipper2Lib::Path64 const& a, Clipper2Lib::Path64 const& b)
		{
			if (a.size() != b.size())
			{
				return false;
			}

			auto doubled = a;
			doubled.insert(doubled.end(), a.begin(), a.end());

			return ranges::search(doubled, b).begin() != doubled.end();
		}

		void ClipperUtils::canonicalisePolygon(Clipper2Lib::Path64& polygon)
		{
			rotate(polygon.begin(), polygon.begin() + booth(polygon), polygon.end());
		}

	} // core
} // bw