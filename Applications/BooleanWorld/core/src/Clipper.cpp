#include <queue>
#include <set>
#include <unordered_map>

#include "core/ClipperDefines.h"
#include "core/Clipper.h"
#include "core/ClipperUtils.h"
#include "core/World.h"


using namespace std;


namespace bw
{
	namespace core
	{

		using namespace std;

		struct EdgeVertex
		{
			double x, y;

			auto operator<=>(EdgeVertex const&) const = default;
		};

		struct EdgeEntry
		{
			EdgeVertex a, b;

			EdgeEntry(EdgeVertex a_, EdgeVertex b_)
			{
				if (a_ < b_)
				{
					swap(a_, b_);
				}

				a = a_;
				b = b_;
			}

			auto operator<=>(EdgeEntry const&) const = default;
		};

		Clipper::Clipper(vector<WorldVertexData> const& baseWorldVertexData, vector<Clipper2Lib::Paths64> const& intermediateStates, World const* world, uint32_t flags)
			: mBaseWorldVertexData(baseWorldVertexData)
			, mCallback(baseWorldVertexData, flags)
			, mwWorld(world)
			, mFlags(flags)
		{
		}

		vector<WorldVertexData> const& Clipper::getBaseWorldVertexData() const
		{
			return mBaseWorldVertexData;
		}

		vector<WorldVertexData> const& Clipper::getClippedWorldVertexData() const
		{
			return mClippedWorldVertexData;
		}

		vector<Clipper2Polygon> const& Clipper::getBorderPolygons() const
		{
			return mBorder;
		}

		graph::PolygonGraph const& Clipper::getArrangementGraph() const
		{
			return mArrangementGraph;
		}

		ClipStats Clipper::getStats() const
		{
			return mStats;
		}

		vector<Clipper2Polygon> Clipper::executeClip(Clipper2Lib::Clipper64& clipper, Clipper2Lib::ClipType op, Clipper2Lib::FillRule fillRule, PrimitivePropertySet const& clipProperties)
		{
			Clipper2Lib::PolyTree64 polytree;

			mCallback.setClipType(op);

			clipper.SetZCallback(std::bind(&clipper2::ZCallback::interpolateVertex,
				&mCallback,
				std::placeholders::_1,
				std::placeholders::_2,
				std::placeholders::_3,
				std::placeholders::_4,
				std::placeholders::_5)
			);

			clipper.Execute(op, fillRule, polytree);

			// Go over polytree.  Root will be a collection of filled (non-hole) polygons.  These
			// need to be iterated over depth-first, adding all their holes.  Each added hole
			// should be added to a list, and afterwards, iterate over this list, treating the
			// hole as a root.
			vector<Clipper2Polygon> outPaths;
			ClipperUtils::traverseTree(&polytree, outPaths, false, clipProperties, mCallback.getVertexWorldData());

			return outPaths;
		}

		void Clipper::setPolygonPrimitiveIndex(Clipper2Lib::Paths64& polygons, uint32_t primitiveIndex)
		{
			for (auto& polygon : polygons)
			{
				for (auto& vertex : polygon)
				{
					vertex.z = BW_VERTEX_Z_PACK_PRIMITIVE_INDEX(vertex.z, primitiveIndex);
				}
			}
		}

		void Clipper::setPolygonPrimitiveIndex(vector<Clipper2Polygon>& polygons, uint32_t primitiveIndex)
		{
			for (auto& polygon : polygons)
			{
				for (auto& vertex : polygon.path)
				{
					vertex.z = BW_VERTEX_Z_PACK_PRIMITIVE_INDEX(vertex.z, primitiveIndex);
				}

				polygon.primitiveIndex = primitiveIndex;
			}
		}

		void Clipper::addIntermediateClipping(Clipper2Lib::Paths64 const& paths, uint32_t primitiveIndex, vector<Clipper2Lib::Paths64>& states)
		{
			states.push_back(paths);
			setPolygonPrimitiveIndex(states.back(), primitiveIndex);
		}

		vector<Clipper2Lib::Paths64> Clipper::generateIntermediateClippings(vector<ClipData> const& paths)
		{
			vector<Clipper2Lib::Paths64> intermediateStates;

			auto numPaths = (uint32_t)paths.size();
			auto workingPathSet = paths[0].paths;
			auto primitiveIndex = (uint32_t)(BW_VERTEX_Z_UNPACK_PRIMITIVE_INDEX(workingPathSet[0][0].z));

			if (numPaths == 1)
			{
				addIntermediateClipping(workingPathSet, primitiveIndex, intermediateStates);
			}
			else
			{
				// Apply each primitive to the current result in turn
				for (uint32_t i = 1; i < numPaths; ++i)
				{
					// Clip
					Clipper2Lib::Clipper64 clipper;

					auto const& clipData = paths[i];

					// If we're saving state, do so now
					if (clipData.saveBeforeClip)
					{
						addIntermediateClipping(workingPathSet, primitiveIndex, intermediateStates);

						workingPathSet = clipData.paths;
						primitiveIndex = (uint32_t)(BW_VERTEX_Z_UNPACK_PRIMITIVE_INDEX(workingPathSet[0][0].z));

						continue;
					}

					clipper.AddSubject(workingPathSet);
					clipper.AddClip(clipData.paths);

					// Get clip properties in case we need them for setting interpolated vertex data
					auto clipIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(clipData.paths[0][0].z);
					auto const& clipProperties = mCallback.getVertexWorldData()[clipIndex].properties[0];
					auto result = executeClip(clipper, clipData.ct, clipData.fr, clipProperties);

					// Store result
					workingPathSet.clear();

					for (auto const& clippedPoly : result)
					{
						workingPathSet.push_back(clippedPoly.path);
					}
				}

				if (!workingPathSet.empty())
				{
					addIntermediateClipping(workingPathSet, primitiveIndex, intermediateStates);
				}
			}

			return intermediateStates;
		}

		vector<Clipper2Polygon> Clipper::clipIntermediateClippings(vector<Clipper2Lib::Paths64> const& states, Clipper2Lib::ClipType clipType, bool interpolate)
		{
			vector<Clipper2Polygon> result{};

			if (states.empty())
			{
				return result;
			}
			else if (states.size() == 1)
			{
				auto const& state = states[0];
				auto numStatePaths = (uint32_t)state.size();

				for (uint32_t i = 0; i < numStatePaths; ++i)
				{
					result.push_back({ i > 0, ~0u, state[i] });
				}

				return result;
			}
			
			auto workingPathSet = states[0];
			auto numStates = (uint32_t)states.size();

			for (uint32_t i = 1; i < numStates; ++i)
			{
				Clipper2Lib::Clipper64 clipper;

				clipper.AddSubject(workingPathSet);
				clipper.AddClip(states[i]);

				if (interpolate)
				{
					mCallback.setClipType(clipType);

					clipper.SetZCallback(std::bind(&clipper2::ZCallback::interpolateVertex,
						&mCallback,
						std::placeholders::_1,
						std::placeholders::_2,
						std::placeholders::_3,
						std::placeholders::_4,
						std::placeholders::_5)
					);
				}

				// Get clip properties in case we need them for setting interpolated vertex data
				auto clipIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(states[i][0][0].z);
				auto const& clipProperties = mCallback.getVertexWorldData()[clipIndex].properties[0];

				if (i == (numStates - 1))
				{
					Clipper2Lib::PolyTree64 polytree;

					clipper.Execute(clipType, Clipper2Lib::FillRule::NonZero, polytree);
					ClipperUtils::traverseTree(&polytree, result, true, clipProperties, mCallback.getVertexWorldData());
				}
				else
				{
					Clipper2Lib::Paths64 tempRes;

					clipper.Execute(clipType, Clipper2Lib::FillRule::NonZero, tempRes);
					workingPathSet = tempRes;

					for (auto& path : workingPathSet)
					{
						ClipperUtils::interpolatePathVertices(path, clipProperties, mCallback.getVertexWorldData());
					}

					// Go through each polygon in workingPathSet, and for each:
					// - Find the first non-intersection vertex, or intersection vertex which has
					//   both of its properties set.
					// - From that vertex, V, iterate until we reach V again (may go past the end
					//   of the list):
					// - If Vprev not set, Vprev= (V-1)next
					// - If Vnext not set:
					//   - If (V+1)prev is set, use that
					//   - Else, set Vnext to Vprev
				}
			}

			// Union to remove self-intersects and pinchpoints

			return result;
		}

		vector<Clipper2Polygon> Clipper::clipIntermediateClipping(Clipper2Lib::Paths64 const& state, vector<Clipper2Polygon> const& polygons, Clipper2Lib::ClipType clipType, uint32_t primitiveIndex)
		{
			Clipper2Lib::Clipper64 clipper;

			clipper.AddSubject(state);

			Clipper2Lib::Paths64 clipPaths;

			for (auto const& polygon : polygons)
			{
				clipPaths.push_back(polygon.path);
			}

			clipper.AddClip(clipPaths);

			vector<Clipper2Polygon> result;
			Clipper2Lib::PolyTree64 polytree;

			clipper.Execute(clipType, Clipper2Lib::FillRule::NonZero, polytree);

			// Get clip properties in case we need them for setting interpolated vertex data
			auto clipIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(clipPaths[0][0].z);
			auto const& clipProperties = mCallback.getVertexWorldData()[clipIndex].properties[0];

			ClipperUtils::traverseTree(&polytree, result, true, clipProperties, mCallback.getVertexWorldData());
			
			setPolygonPrimitiveIndex(result, primitiveIndex);

			return result;
		}

		void Clipper::calculateCombinedPolygons(Clipper2Lib::Paths64 const& interState, vector<Clipper2Polygon> const& arrangePolygons, Clipper2Lib::ClipType clipType, uint32_t primitiveIndex, vector<Clipper2Lib::Paths64>& combinedPaths)
		{
			auto polys = clipIntermediateClipping(interState, arrangePolygons, clipType, primitiveIndex);

			if (!polys.empty())
			{
				Clipper2Lib::Paths64 combinedPath{ polys[0].path };

				for (uint32_t i = 1; i < polys.size(); ++i)
				{
					auto const& poly = polys[i];

					if (!poly.isHole)
					{
						combinedPaths.push_back(combinedPath);
						combinedPath.clear();
					}

					combinedPath.push_back(poly.path);
				}

				combinedPaths.push_back(combinedPath);
			}
		}

		void Clipper::buildPolygonGraph(vector<Clipper2Polygon> const& polygons)
		{
			mArrangementGraph.clear();

			unordered_map<graph::PolygonGraphVertex, size_t, graph::PolygonGraphVertexHash> vertexMap;
			unordered_map<graph::PolygonGraphEdge, size_t, graph::PolygonGraphEdgeHash> edgeMap;

			auto getVertex = [&](Clipper2Lib::Point64 const& p) -> uint32_t
			{
				graph::PolygonGraphVertex pgv{ p.x / BW_CLIPPER_SCALE, p.y / BW_CLIPPER_SCALE, p.z };

				auto it = vertexMap.find(pgv);

				if (it != vertexMap.end())
				{
					return it->second;
				}

				auto idx = (uint32_t)mArrangementGraph.vertices.size();
				mArrangementGraph.vertices.push_back(pgv);
				vertexMap.emplace(pgv, idx);

				return idx;
			};

			for (auto const& polygon : polygons)
			{
				auto nv = (uint32_t)polygon.path.size();

				for (uint32_t i = 0; i < nv; ++i)
				{
					uint32_t j = (i + 1) % nv;

					auto a = getVertex(polygon.path[i]);
					auto b = getVertex(polygon.path[j]);

					// Insert edge
					auto edge = graph::PolygonGraphEdge{ { a, b } };
					auto it = edgeMap.find(edge);

					if (it != edgeMap.end())
					{
						auto& e = mArrangementGraph.edges[it->second];

						// Edge already exists so the there must be a polygon on the other
						// side, and as we can't have walls between touching polygons, set
						// the primitiveIndex.  However, it we have polygons on top of each
						// other in such a way that they share an exact edge, then we have
						// a duplicate to remove.  In this circumstance, the vertex ordering
						// will be the same
						if ((a == e.v[1] && b == e.v[0]) // If the order is the same, they're on top of each other
							&& e.p[1] == ~0u) // If the second index has been set, this must be a duplicate
						{
							e.p[1] = polygon.primitiveIndex;
						}
					}
					else
					{
						edge.p[0] = polygon.primitiveIndex;
						
						auto idx = (uint32_t)mArrangementGraph.edges.size();
						edgeMap[edge] = idx;

						mArrangementGraph.edges.push_back(edge);
					}
				}
			}
		}

		vector<Clipper2Polygon> Clipper::clip(vector<ClipData> const& paths)
		{
			/*
			This function does more than simply clip a set of polygons together!
			It produces a full set of data structures for rendering and collision.
			The process here is as follows:
			- We break the clip list up into sub-lists, divided by union operations.
			- Each sub-list is processed into an "intermediate clipping".  During this,
			  vertices are interpolated and stored in the callback.
			- We union all the intermediate clippings together, interpolating vertices,
			  in order to produce the standard final result.  We split the intermediate
			  clippings up first, so we have a set of polygons with defined Primitives:
			  each intermediate clipping assumes the Primitive of the first path in the list.
			- [At this point, we have the standard border and interpolated vertex data]
			- We XOR all the intermediate clippings together (without interpolation: at
			  this point, we have all the interpolated vertices we need), to cut everything
			  up: while we have all the clipped intermediate polygons with their assigned
			  Primitive, there will be overlaps.  These overlaps are removed by union of course,
			  but this also merges polygons with different Primitives, which we don't want.
			  So by XORing everything, we end up with a template to cut individual polygons out of.
			- We then diff and intersect each intermediate clipping against this template, to
			  produce a set of polygons.  Some polygons will overlap exactly.
			  - We remove the duplicates in two ways:
			    - When building the graph, we don't add the same line in the same direction.
			    - When creating the triangulation, we store the centre of each triangle added.
			      Because polygons overlap perfectly, there will be duplicate centre points for
			      overlapping polygons.
			- Finally we create the graph, which is used for collisions, and wall rendering,
			  and return the cut polygons for floor/ceiling physics and rendering.
			*/
			vector<Clipper2Polygon> result;

			// 1. Generate intermediate clippings
			//    - Interpolate vertices as normal
			//    - All intermediate clipping vertices should have primitiveIndex set to the first Primitive in the clipping
			auto interStates = generateIntermediateClippings(paths);

			// TODO: multi-threading
			// - We can do steps 2 and 3 in parallel
			// - We can do all the calculateCombinedPolygons() calls in parallel

			// 2. Union the intermediate clippings to create border clipping
			//    - Interpolate vertices as normal, set primitiveIndex to -1
			mBorder = clipIntermediateClippings(interStates, Clipper2Lib::ClipType::Union, true);
			mClippedWorldVertexData = mCallback.getVertexWorldData();

			// 3. XOR the intermediate clippings to create arrangement clipping (no ZCallback needed)
			auto arrangePolygons = clipIntermediateClippings(interStates, Clipper2Lib::ClipType::Xor, false);

			// 4. For each intermediate:
			//    - Diff it against the arrangement clipping and set the resulting polgon vertices primitiveIndex to the intermediate's
			//    - Intersect it against the arrangement clipping and set the resulting polgon vertices primitiveIndex to the intermediate's
			//    - Build up a list of Clipper2 paths for dupe-checking as we do this.
			vector<Clipper2Lib::Paths64> combinedPaths;

			for (auto const& interState : interStates)
			{
				uint32_t primitiveIndex = (uint32_t)(BW_VERTEX_Z_UNPACK_PRIMITIVE_INDEX(interState[0][0].z));

				calculateCombinedPolygons(interState, arrangePolygons, Clipper2Lib::ClipType::Difference, primitiveIndex, combinedPaths);
				calculateCombinedPolygons(interState, arrangePolygons, Clipper2Lib::ClipType::Intersection, primitiveIndex, combinedPaths);
			}

			// 5. Convert to Clipper2Polygons
			for (auto& paths : combinedPaths)
			{
				auto primitiveIndex = (uint32_t)(BW_VERTEX_Z_UNPACK_PRIMITIVE_INDEX(paths[0][0].z));

				for (uint32_t i = 0; i < paths.size(); ++i)
				{
					// Canonicalise to ensure triangulations of duplicates are the same.  If the ordering of
					// the path vertices is different, the triangulator may produce sets of triangles which cover
					// the same area but use different triangles, which will mean we can't determine which are
					// duplicates.
					ClipperUtils::canonicalisePolygon(paths[i]);

					result.push_back({ i != 0, primitiveIndex, paths[i] });
			
					mStats.verticesGenerated += (uint32_t)paths[i].size();
				}
			}

			// 6. Create graph for removing duplicates and collisions
			buildPolygonGraph(mBorder);

			return result;
		}

		vector<Clipper2Polygon> Clipper::clipToClipper2Polygons(vector<Primitive*> const& primitives, Primitive::Operation unionReplacementOp, wp::BoundingBox const* bounds)
		{
			// Build clip data
			vector<ClipData> clipData;
			
			for (auto const primitive : primitives)
			{
				auto paths = ClipperUtils::convertComplexPolygonsToPath(primitive);

				mStats.primitivesProcessed++;
				mStats.primVerticesProcessed += primitive->getNumVertices();

				Clipper2Lib::FillRule fillRule{ Clipper2Lib::FillRule::NonZero };
				Clipper2Lib::ClipType clipType{ Clipper2Lib::ClipType::NoClip };
				
				switch (primitive->getFillRule())
				{
				case Primitive::FillRule::EvenOdd:
					fillRule = Clipper2Lib::FillRule::EvenOdd;
					break;

				case Primitive::FillRule::NonZero:
					fillRule = Clipper2Lib::FillRule::NonZero;
					break;

				default:
					throw exception("Unknown fill rule");
				}
				
				bool saveState{ false };

				switch (primitive->getOperation())
				{
				// If we hit a Union, then we want to generate an intermediate clipping here
				case Primitive::Operation::Union:
					saveState = (mFlags & BW_CLIPPER_GEN_INTER_ON_UNION) != 0;

					switch (unionReplacementOp)
					{
					case Primitive::Operation::Union:
						clipType = Clipper2Lib::ClipType::Union;
						break;

					case Primitive::Operation::Intersection:
						clipType = Clipper2Lib::ClipType::Intersection;
						break;

					case Primitive::Operation::Difference:
						clipType = Clipper2Lib::ClipType::Difference;
						break;

					case Primitive::Operation::XOR:
						clipType = Clipper2Lib::ClipType::Xor;
						break;

					default:
						throw exception("Unknown boolean operation");
					}
					break;

				case Primitive::Operation::Intersection:
					clipType = Clipper2Lib::ClipType::Intersection;
					break;

				case Primitive::Operation::Difference:
					clipType = Clipper2Lib::ClipType::Difference;
					break;

				case Primitive::Operation::XOR:
					clipType = Clipper2Lib::ClipType::Xor;
					break;

				default:
					throw exception("Unknown boolean operation");
				}

				clipData.push_back({ paths, clipType, fillRule, saveState });
			}

			if (bounds)
			{
				wp::Vector2 minExtent, maxExtent;

				bounds->getExtents(minExtent, maxExtent);

				Clipper2Lib::Path64 clipRect;

				int64_t clipZ{ 0 };

				clipZ = BW_VERTEX_Z_PACK_VERTEX_INDEX(clipZ, BW_VERTEX_RECTCLIP_ID);

				clipRect.push_back(BW_CLIPPER_MAKE_POINT(maxExtent.x, maxExtent.y, clipZ));
				clipRect.push_back(BW_CLIPPER_MAKE_POINT(minExtent.x, maxExtent.y, clipZ));
				clipRect.push_back(BW_CLIPPER_MAKE_POINT(minExtent.x, minExtent.y, clipZ));
				clipRect.push_back(BW_CLIPPER_MAKE_POINT(maxExtent.x, minExtent.y, clipZ));

				clipData.push_back({ { clipRect }, Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::NonZero });
			}

			// Perform clipping
			mStats.verticesGenerated = 0;
			mStats.polygonsGenerated = 0;

			auto clipper2Polygons = clip(clipData);

			mStats.polygonsGenerated = (uint32_t)clipper2Polygons.size();
			mStats.interpolatedVertices = mCallback.getNumInterpolatedVertices();

			return clipper2Polygons;
		}

		vector<ClippedPolygon> Clipper::clipToClippedPolygons(vector<Primitive*> const& primitives, Primitive::Operation unionReplacementOp, wp::BoundingBox const* bounds)
		{
			auto clipper2Polygons = clipToClipper2Polygons(primitives, unionReplacementOp, bounds);
			return ClipperUtils::convertClipper2PolygonsToClippedPolygons(clipper2Polygons, nullptr);
		}

	} // core
} // bw