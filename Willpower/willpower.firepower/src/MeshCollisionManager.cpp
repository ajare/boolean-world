#include "willpower/common/Globals.h"
#include "willpower/common/MathsUtils.h"

#include "willpower/firepower/MeshCollisionManager.h"

#define EQ_EPSILON(x,y) (x >= ((y) - MathsUtils::Epsilon) && x <= ((y) + MathsUtils::Epsilon))
#define LTE_EPSILON(x,y) (x <= ((y) + MathsUtils::Epsilon))
#define GTE_EPSILON(x,y) (x >= ((y) - MathsUtils::Epsilon))

namespace WP_NAMESPACE
{
	namespace firepower
	{
		using namespace std;

		MeshCollisionManager::MeshCollisionManager(ExtentsCalculator const& extents, uint32_t cellsX, uint32_t cellsY)
			: mCellsX(cellsX)
			, mCellsY(cellsY)
		{
			// Set up grid
			mOrigin = extents.getMinExtent();
			mSize = extents.getSize();

			auto const& cellSize = extents.getCellSize(cellsX, cellsY);
			mCellSizeX = cellSize.x;
			mCellSizeY = cellSize.y;

			mCells.resize(mCellsX * mCellsY);

			// Bomb/beam working variables
			mBombLines.reserve(32);
			mBombStartEnds.reserve(32);
			mVerticesToCheck.reserve(64);
		}

		CachedStaticAccelerationGrid const* MeshCollisionManager::getGrid() const
		{
			return nullptr;
		}

		void MeshCollisionManager::addEdge(Vector2 const& v0, Vector2 const& v1, uint32_t v0Index, uint32_t v1Index, uint32_t edgeIndex, BoundingBox const& bounds)
		{
			Vector2 minEdgeExtent = bounds.getMinExtent() - mOrigin;
			Vector2 maxEdgeExtent = bounds.getMaxExtent() - mOrigin;

			int edgeX0 = (int)(minEdgeExtent.x / mCellSizeX);
			int edgeY0 = (int)(minEdgeExtent.y / mCellSizeY);
			int edgeX1 = (int)(maxEdgeExtent.x / mCellSizeX);
			int edgeY1 = (int)(maxEdgeExtent.y / mCellSizeY);

			if (edgeX0 < 0 || edgeY0 < 0 || edgeX1 >= mCellsX || edgeY1 >= mCellsY)
			{
				throw Exception("Edge out of bounds.");
			}

			for (int y = edgeY0; y <= edgeY1; ++y)
			{
				for (int x = edgeX0; x <= edgeX1; ++x)
				{
					Vector2 cell0(x * mCellSizeX, y * mCellSizeY);
					cell0 += mOrigin;

					Vector2 cell1 = cell0 + Vector2(mCellSizeX, mCellSizeY);

					// Check whether edge intersects cell
					if (MathsUtils::lineBoxIntersection(v0, v1, cell0, cell1) !=
						MathsUtils::LineIntersectionType::NotIntersecting)
					{
						// Clip if required.
						// TODO: clipLineAgainstQuad is incorrect as it can arbitrarily reverse
						// the direction, if neither vertex is in the box.  It's also slow,
						// we don't need to do full line-line intersections when one line is
						// axis-aligned.
						vector<Vector2> clippedLine = MathsUtils::clipLineAgainstQuad(v0, v1, cell0, cell1);

						// TODO: improve on this slow hack.
						if (v0.distanceToSq(clippedLine.front()) > v0.distanceToSq(clippedLine.back()))
						{
							swap(clippedLine[0], clippedLine[1]);
						}

						CollisionEdgeEntry entry;

						Cell& cell = mCells[y * mCellsX + x];

						// Add vertices: check if either/both vertices are in this cell.
						if (MathsUtils::pointInBox(v0, cell0, cell1))
						{
							cell.vertexIndices.insert(v0Index);
						}
						if (MathsUtils::pointInBox(v1, cell0, cell1))
						{
							cell.vertexIndices.insert(v1Index);
						}

						// Set up entry for this cell
						entry.v0 = clippedLine.front();
						entry.v1 = clippedLine.back();
						entry.index = edgeIndex;

						cell.edgeEntries.push_back(entry);
					}
				}
			}
		}

		bool MeshCollisionManager::lineIntersectsMesh(Vector2 const& v0, Vector2 const& v1, Vector2 const& dir, Vertex const& ignoreVertex) const
		{
			uint32_t ignore0 = ignoreVertex.e[0];
			uint32_t ignore1 = ignoreVertex.e[1];

			auto numEdges = (uint32_t)mMesh.edges.size();
			for (uint32_t i = 0; i < numEdges; ++i)
			{
				if (i == ignore0 || i == ignore1)
				{
					continue;
				}

				auto const& edge = mMesh.edges[i];

				if (edge.n.dot(dir) < 0)
				{
					continue;
				}

				if (MathsUtils::lineIntersectsLine(v0, v1, edge.v[0], edge.v[1]) ==
					MathsUtils::LineIntersectionType::Intersecting)
				{
					return true;
				}
			}

			return false;
		}

		Vector2 MeshCollisionManager::projectLineThroughMesh(Vector2 const& v0, Vector2 const& v1, Vertex const* ignoreVertex, bool* isFull) const
		{
			float hitDist = 1e10f;
			Vector2 hitPos = v1;
			Vector2 dir = v0.directionTo(v1);

			if (isFull)
			{
				*isFull = true;
			}

			int ignore0 = ignoreVertex ? (int)ignoreVertex->e[0] : -1;
			int ignore1 = ignoreVertex ? (int)ignoreVertex->e[1] : -1;
			
			auto numEdges = (int)mMesh.edges.size();
			for (int i = 0; i < numEdges; ++i)
			{
				if (i == ignore0 || i == ignore1)
				{
					continue;
				}

				auto const& edge = mMesh.edges[i];

				if (edge.n.dot(dir) >= 0)
				{
					continue;
				}

				LineHit hit;
				if (MathsUtils::lineLineIntersection(v0, v1, edge.v[0], edge.v[1], &hit) !=
					MathsUtils::LineIntersectionType::NotIntersecting)
				{
					if (!hit.isTouching())
					{
						float d = hit.getPosition().distanceToSq(v0);
						if (d < hitDist)
						{
							hitPos = hit.getPosition();
							hitDist = d;
						}

						if (isFull)
						{
							*isFull = false;
						}
					}
				}
			}

			return hitPos;
		}

		Edge const& MeshCollisionManager::getCollisionEdge(uint32_t index) const
		{
			return mMesh.edges[index];
		}

		int MeshCollisionManager::MeshCollisionManager::getNumCollisionEdges() const
		{
			return (int)mMesh.edges.size();
		}

		Mesh const& MeshCollisionManager::getMesh() const
		{
			return mMesh;
		}

		void MeshCollisionManager::addMesh(geometry::Mesh const* mesh)
		{
			addMesh(mesh, [](geometry::Mesh const* m, uint32_t edgeIndex) 
			{ 
				auto const& edge = m->getEdge(edgeIndex);
				return edge.getConnectivity() == geometry::Edge::Connectivity::External;
			});
		}

		void MeshCollisionManager::addMesh(geometry::Mesh const* mesh, geometry::Mesh::EdgeFilterFunction edgeFilterFn)
		{
			// Get edges to add.  Add by polygon as we need to make sure they 
			// are wound correctly.
			uint32_t polygonIndex = mesh->getFirstPolygonIndex();
			while (!mesh->polygonIndexIterationFinished(polygonIndex))
			{
				auto const& polygon = mesh->getPolygon(polygonIndex);

				// Get all edges in polygon
				vector<geometry::DirectedEdge> directedEdges;

				auto edgeIt = polygon.getFirstEdge();
				while (edgeIt != polygon.getEndEdge())
				{
					geometry::DirectedEdge de = *edgeIt;
					if (edgeFilterFn(mesh, de.index))
					{
						directedEdges.push_back(de);
					}

					edgeIt++;
				}

				// Append the vertices to mesh.vertices.  The polygon
				// is a loop so we just need to add de.v0.
				auto vertexOffset = (uint32_t)mMesh.vertices.size();
				auto edgeOffset = (uint32_t)mMesh.edges.size();
				auto numEdges = (uint32_t)directedEdges.size();

				for (uint32_t i = 0; i < numEdges; ++i)
				{
					auto const& de = directedEdges[i];

					// Add vertex to base mesh, calculating:
					// Position
					// Normal
					// Normal of the edges that it's a member of
					Vertex v;

					v.p = mesh->getVertex(de.v0).getPosition();

					int prevI = i == 0 ? numEdges - 1 : i - 1;

					Vector2 v0 = mesh->getVertex(directedEdges[prevI].v0).getPosition();
					Vector2 v1 = mesh->getVertex(directedEdges[i].v1).getPosition();

					Vector2 e0n = (v.p - v0).normalisedCopy().perpendicular();
					Vector2 e1n = (v1 - v.p).normalisedCopy().perpendicular();
					v.n = (e0n + e1n).normalisedCopy();

					v.e[0] = prevI + edgeOffset;
					v.e[1] = i + edgeOffset;

					mMesh.vertices.push_back(v);

					// Add edge, calculating:
					// Vertex indices
					// Vertex positions
					// Normal
					Edge e;

					e.vi[0] = i + vertexOffset;
					e.vi[1] = ((i + 1) % numEdges) + vertexOffset;
					e.v[0] = v.p;
					e.v[1] = v1;
					e.n = e1n;

					mMesh.edges.push_back(e);

					// Add edge to lookup
					auto const& edge = mesh->getEdge(de.index);
					auto const& edgeBounds = edge.getBoundingBox();

					addEdge(v.p, v1, e.vi[0], e.vi[1], (uint32_t)mMesh.edges.size() - 1, edgeBounds);
				}

				polygonIndex = mesh->getNextPolygonIndex(polygonIndex);
			}
		}

		void MeshCollisionManager::addStaticPolygon(vector<Vector2> const& points)
		{
			WP_UNUSED(points);

			/*
			int vOffset = mMesh.vertices.size();

			vector<Vertex> vertices;
			for (auto const& p : points)
			{
				Vertex v;
				v.p = p;

				vertices.push_back(v);
			}

			copy(vertices.begin(), vertices.end(), back_inserter(mMesh.vertices));

			uint32_t vCount = mMesh.vertices.size() - vOffset;
			uint32_t eOffset = mMesh.edges.size();
			for (uint32_t i = 0; i < vCount; ++i)
			{
				Edge e;

				e.vi[0] = i + vOffset;
				e.vi[1] = ((i + 1) % vCount) + vOffset;
				e.v[0] = mMesh.vertices[e.vi[0]].p;
				e.v[1] = mMesh.vertices[e.vi[1]].p;
				e.n = -(e.v[1] - e.v[0]).normalisedCopy().perpendicular();

				mMesh.edges.push_back(e);
				mMesh.vertexEdges.push_back(make_pair((i == 0 ? (vCount - 1) : i - 1) + vOffset, i + vOffset));

				BoundingBox bb({ e.v[0], e.v[1] });
				addEdge(vOffset, i, vCount, e.v[0], e.v[1], e.n, eOffset + vCount, bb);
			}

			// Vertex normals
			for (uint32_t i = vOffset; i < (int)mMesh.vertices.size(); ++i)
			{
				auto& v = mMesh.vertices[i];
				auto const& e0 = mMesh.edges[mMesh.vertexEdges[i].first];
				auto const& e1 = mMesh.edges[mMesh.vertexEdges[i].second];

				v.n = (e0.n + e1.n).normalisedCopy();
			}
			*/
		}


		void MeshCollisionManager::addStaticOBB(Vector2 const& minExtent, Vector2 const& maxExtent, float angle)
		{
			Vector2 centre = (maxExtent + minExtent) * 0.5f;

			vector<Vector2> points = {
				minExtent,
				Vector2(minExtent.x, maxExtent.y),
				maxExtent,
				Vector2(maxExtent.x, minExtent.y)
			};

			for (auto& point: points)
			{
				point.rotateClockwiseAround(centre, angle);
			}

			addStaticPolygon(points);
		}

		void MeshCollisionManager::addStaticAABB(Vector2 const& minExtent, Vector2 const& maxExtent)
		{
			addStaticOBB(minExtent, maxExtent, 0.0f);
		}

		void MeshCollisionManager::getExtents(Vector2& minExtent, Vector2& maxExtent) const
		{
			minExtent.set(numeric_limits<float>::max(), numeric_limits<float>::max());
			maxExtent.set(numeric_limits<float>::lowest(), numeric_limits<float>::lowest());

			auto numEdges = (int)mMesh.edges.size();
			for (int i = 0; i < numEdges; ++i)
			{
				auto const& edge = mMesh.edges[i];

				for (int j = 0; j < 2; ++j)
				{
					auto const& v = edge.v[j];
					if (v.x < minExtent.x)
					{
						minExtent.x = v.x;
					}
					if (v.y < minExtent.y)
					{
						minExtent.y = v.y;
					}
					if (v.x > maxExtent.x)
					{
						maxExtent.x = v.x;
					}
					if (v.y > maxExtent.y)
					{
						maxExtent.y = v.y;
					}
				}
			}
		}

		int32_t MeshCollisionManager::checkBullet(Vector2 const& oldPosition, Vector2 const& newPosition, float radius, float checkTunnelling)
		{
			// TODO: clip to bounds, or ensure that the bullet won't move outside them
			// in one frame.  If outside, then kill it.
			int x0 = (int)((newPosition.x - radius - mOrigin.x) / mCellSizeX);
			int y0 = (int)((newPosition.y - radius - mOrigin.y) / mCellSizeY);
			int x1 = (int)((newPosition.x + radius - mOrigin.x) / mCellSizeX);
			int y1 = (int)((newPosition.y + radius - mOrigin.y) / mCellSizeY);

			// Go through all cells the bullet is in
			float radiusSq = radius * radius;
			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					Cell const& cell = mCells[y * mCellsX + x];
					
					// Get distance from bullet position to edge
					for (auto const& edgeEntry: cell.edgeEntries)
					{
						float distSq = newPosition.distanceToLineSq(edgeEntry.v0, edgeEntry.v1);
						if (distSq <= radiusSq)
						{
							return edgeEntry.index;
						}
					}

					if (checkTunnelling)
					{
						for (auto const& edgeEntry: cell.edgeEntries)
						{
							auto const& v0 = edgeEntry.v0;
							auto const& v1 = edgeEntry.v1;

							int newSign = MathsUtils::valueSign(
								(v1.x - v0.x) * (newPosition.y - v0.y) -
								(v1.y - v0.y) * (newPosition.x - v0.x));

							int oldSign = MathsUtils::valueSign(
								(v1.x - v0.x) * (oldPosition.y - v0.y) -
								(v1.y - v0.y) * (oldPosition.x - v0.x));

							if (newSign != oldSign)
							{
								return edgeEntry.index;
 							}
						}
					}
				}
			}

			return -1;
		}

		vector<Bomb::Arc> MeshCollisionManager::getFullBomb(Bomb const& bomb)
		{
			WP_UNUSED(bomb);
			return {};
		}

		vector<Bomb::Arc> MeshCollisionManager::checkBomb(Bomb const& bomb) const
		{
			mBombLines.clear();
			mBombStartEnds.clear();

			Vector2 const& bombPosition = bomb.getPosition();
			float bombRadius = bomb.getRadius();

			// Get vertices and edges within bounding circle
			int x0 = (int)((bombPosition.x - bombRadius - mOrigin.x) / mCellSizeX);
			int y0 = (int)((bombPosition.y - bombRadius - mOrigin.y) / mCellSizeY);
			int x1 = (int)((bombPosition.x + bombRadius - mOrigin.x) / mCellSizeX);
			int y1 = (int)((bombPosition.y + bombRadius - mOrigin.y) / mCellSizeY);

			x0 = max(0, min(x0, mCellsX - 1));
			y0 = max(0, min(y0, mCellsY - 1));
			x1 = max(0, min(x1, mCellsX - 1));
			y1 = max(0, min(y1, mCellsY - 1));

			// Intersect circle and get extra vertices to check
			mVerticesToCheck.clear();

			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					Cell const& cell = mCells[y * mCellsX + x];

					// Vertices
					for (uint32_t vertexIndex: cell.vertexIndices)
					{
						mVerticesToCheck.push_back(mMesh.vertices[vertexIndex]);
					}

					for (auto const& edgeEntry: cell.edgeEntries)
					{
						auto const& edge = mMesh.edges[edgeEntry.index];

						// If the edge 'enters' the circle, then (due to anticlockwise winding), this point marks
						// the end of an arc.  Exiting the circle means it's the start of an arc.  If it enters and
						// then exits, then the first point is the end of an arc, and the second is the start of the
						// next one.
						Vertex fv;
						Vector2 fvDir;
						LineHit hit1, hit2;
						auto iType = MathsUtils::lineCircleIntersection(edgeEntry.v0, edgeEntry.v1, bombPosition, bombRadius, &hit1, &hit2);

						switch (iType)
						{
						case MathsUtils::LineIntersectionType::DoublyIntersecting:
							// Add hit(s) to list of vertices to intersect, unless normal is at 90 degrees
							// to the direction to the centre
							fv.p = hit2.getPosition();
							fvDir = bombPosition.directionTo(fv.p);

							if (!GTE_EPSILON(fvDir.dot(edge.n), 0.0f))
							{
								fv.p -= fvDir * MathsUtils::Epsilon;
								fv.e[0] = edgeEntry.index;
								fv.e[1] = edgeEntry.index;

								mVerticesToCheck.push_back(fv);

								mBombStartEnds.push_back(1); // End of an arc
							}
						case MathsUtils::LineIntersectionType::Intersecting:
							fv.p = hit1.getPosition();
							fvDir = bombPosition.directionTo(fv.p);

							if (!GTE_EPSILON(fvDir.dot(edge.n), 0.0f))
							{
								fv.p -= fvDir * MathsUtils::Epsilon;
								fv.e[0] = edgeEntry.index;
								fv.e[1] = edgeEntry.index;

								mVerticesToCheck.push_back(fv);

								int startEnd = (hit1.getFlags() & LineHit::Flags::HitEnters) ? 0 : 1;
								mBombStartEnds.push_back(startEnd);
							}
							break;
						default:
							break;
						}
					}
				}
			}

			// Find vertices within circle
			int addedStartEndIndex = 0;
			for (uint32_t i = 0; i < mVerticesToCheck.size(); ++i)
			{
				auto const& v = mVerticesToCheck[i];
				bool vertexWasAdded = v.e[0] == v.e[1];

				if (!vertexWasAdded)
				{
					// We already know the vertices we added on the end are inside the arc.
					if (!MathsUtils::pointInCircle(v.p, bombPosition, bombRadius))
					{
						continue;
					}
				}

				Vector2 rayDir = bombPosition.directionTo(v.p);

				// Shoot rays down, if they intersect anything, discard them
				if (!lineIntersectsMesh(v.p, bombPosition, -rayDir, v))
				{
					// Check if it's one of the 'full' extra ones added earlier
					if (vertexWasAdded)
					{
						// Add full vertex
						Bomb::Line line;

						line.end = v.p;
						line.angle = (v.p - bombPosition).clockwiseAngle();
						line.startEnd = mBombStartEnds[addedStartEndIndex];

						mBombLines.push_back(line);
					}
					else
					{
						// Check if it is glancing, and if so, add a line to the side of it
						float a0 = rayDir.minimumAngleTo(mMesh.edges[v.e[0]].n);
						float a1 = rayDir.minimumAngleTo(mMesh.edges[v.e[1]].n);

						if ((LTE_EPSILON(a0, 90.0f) && a1 > 90) || (a0 > 90 && LTE_EPSILON(a1, 90.0f)))
						{
							Vector2 start = v.p + v.n * MathsUtils::Epsilon;
							Vector2 end = start + rayDir * (bombRadius - bombPosition.distanceTo(start));

							bool isFull;
							Vector2 hitGlance = projectLineThroughMesh(start, end, nullptr, &isFull);
							if (isFull)
							{
								Bomb::Line line;

								line.end = hitGlance;
								line.angle = (hitGlance - bombPosition).clockwiseAngle();

								// If start is left of line from mPosition to v.p then it's an arc end, and vice versa
								float f = (v.p.y - bombPosition.y) * start.x + (bombPosition.x - v.p.x) * start.y + (v.p.x * bombPosition.y - bombPosition.x * v.p.y);
								if (f < 0.0f)
								{
									line.startEnd = 1;
								}
								else
								{
									line.startEnd = 0;
								}

								mBombLines.push_back(line);
							}
						}
					}
				}

				if (vertexWasAdded)
				{
					addedStartEndIndex++;
				}
			}

			// Sort by angle
			sort(mBombLines.begin(), mBombLines.end(), [](Bomb::Line const& a, Bomb::Line const& b)
			{
				return a.angle < b.angle;
			});
			
			vector<Bomb::Arc> arcs;
			
			size_t numBombLines = mBombLines.size();

			// If mBombLines is empty, then we have a full circle.
			if (numBombLines == 0)
			{
				return arcs;
			}

			// Build resultant arcs
			if (mBombLines[0].startEnd == 0)
			{
				// Start at 0
				for (uint32_t i = 0; i < numBombLines; i += 2)
				{
					Bomb::Arc newArc;

					newArc.first = mBombLines[i].angle;
					newArc.second = mBombLines[i + 1].angle;

					arcs.push_back(newArc);
				}
			}
			else
			{
				// Start at 1
				for (uint32_t i = 1; i < numBombLines; i += 2)
				{
					Bomb::Arc newArc;

					newArc.first = mBombLines[i].angle;
					newArc.second = mBombLines[(i + 1) % numBombLines].angle;
					if (newArc.second < newArc.first)
					{
						newArc.second += 360.0f;
					}

					arcs.push_back(newArc);
				}
			}
			
			return arcs;
		}

		vector<BeamShard> MeshCollisionManager::getFullBeam(Vector2 const& position, float angle, float width, float length)
		{
			// Get beam extents
			auto beamDir = Vector2::fromAngle(angle, Winding::Clockwise);

			auto beamPerp = beamDir.perpendicular();
			auto beamSide = beamPerp * width * 0.5f;
			auto beamExtent = beamDir * length;

			// Get maximum side projections
			auto leftVertexMaxProjected = position + beamSide;
			auto rightVertexMaxProjected = position - beamSide;

			return {
				{ leftVertexMaxProjected, leftVertexMaxProjected + beamExtent, 0, 1 },
				{ rightVertexMaxProjected, rightVertexMaxProjected + beamExtent, 1, 1 }
			};
		}

		vector<BeamShard> MeshCollisionManager::checkBeam(Vector2 const& position, float angle, float width, float length) const
		{
			vector<BeamShard> shards;
			BeamShard workShard;

			mVerticesToCheck.clear();

			// Get beam extents
			Vector2 beamDir = Vector2::fromAngle(angle, Winding::Clockwise);
			float lengthSq = length * length;

			Vector2 beamPerp = beamDir.perpendicular();
			Vector2 beamSide = beamPerp * width * 0.5f;

			// Get maximum side projections
			Vector2 leftVertexMaxProjected = position + beamSide;
			Vector2 rightVertexMaxProjected = position - beamSide;

			bool leftFull, rightFull;
			Vector2 leftBaseVertex = projectLineThroughMesh(position, leftVertexMaxProjected, nullptr, &leftFull);
			Vector2 rightBaseVertex = projectLineThroughMesh(position, rightVertexMaxProjected, nullptr, &rightFull);

			Vector2 beamExtent = beamDir * length;
			if (!leftFull || !rightFull)
			{
				Vector2 centreExtent = projectLineThroughMesh(position, position + beamExtent);

				BeamShard centreShard;
				centreShard.nearVertex = position;
				centreShard.farVertex = centreExtent;
				centreShard.u = 0.5f;
				centreShard.v = position.invLerp(centreExtent, position + beamExtent);

				shards.push_back(centreShard);
			}

			Vector2 leftVertexMaxExtension = leftBaseVertex + beamExtent;
			Vector2 rightVertexMaxExtension = rightBaseVertex + beamExtent;

			// Get vertices and edges within bounding circle
			BoundingBox beamBounds({ leftBaseVertex, rightBaseVertex, leftVertexMaxExtension, rightVertexMaxExtension });

			Vector2 minExtent, maxExtent;
			beamBounds.getExtents(minExtent, maxExtent);

			int x0 = (int)((minExtent.x - mOrigin.x) / mCellSizeX);
			int y0 = (int)((minExtent.y - mOrigin.y) / mCellSizeY);
			int x1 = (int)((maxExtent.x - mOrigin.x) / mCellSizeX);
			int y1 = (int)((maxExtent.y - mOrigin.y) / mCellSizeY);

			x0 = max(0, min(x0, mCellsX - 1));
			y0 = max(0, min(y0, mCellsY - 1));
			x1 = max(0, min(x1, mCellsX - 1));
			y1 = max(0, min(y1, mCellsY - 1));

			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					Cell const& cell = mCells[y * mCellsX + x];

					// Vertices
					for (uint32_t vertexIndex: cell.vertexIndices)
					{
						mVerticesToCheck.push_back(mMesh.vertices[vertexIndex]);
					}

					for (auto const& edge: cell.edgeEntries)
					{
						// Don't use backfacing edges
						if (mMesh.edges[edge.index].n.dot(beamDir) > 0)
						{
							continue;
						}

						wp::LineHit hit;
						if (wp::MathsUtils::lineLineIntersection(leftVertexMaxExtension, rightVertexMaxExtension, edge.v0, edge.v1, &hit) !=
							wp::MathsUtils::LineIntersectionType::NotIntersecting)
						{
							// Add hit.getPosition() to list of vertices to intersect
							Vertex fv;
							fv.p = hit.getPosition();
							fv.e[0] = edge.index;
							fv.e[1] = edge.index;
							mVerticesToCheck.push_back(fv);
						}
					}
				}
			}

			// Get sides, and add left
			if (leftFull)
			{
				Vector2 leftExtent = projectLineThroughMesh(leftBaseVertex, leftVertexMaxExtension);

				workShard.nearVertex = leftBaseVertex;
				workShard.farVertex = leftExtent;
				workShard.u = leftVertexMaxProjected.invLerp(leftBaseVertex, rightVertexMaxProjected);
				workShard.v = leftBaseVertex.invLerp(leftExtent, leftVertexMaxExtension);

				shards.push_back(workShard);
			}

			// Find vertices within beam
			Vector2 leftBaseVertex1 = leftBaseVertex + beamDir;
			Vector2 rightBaseVertex1 = rightBaseVertex + beamDir;

			for (auto const& v: mVerticesToCheck)
			{
				// Check point is within beam
				float f1 = (leftBaseVertex1.y - leftBaseVertex.y) * v.p.x + (leftBaseVertex.x - leftBaseVertex1.x) * v.p.y + (leftBaseVertex1.x * leftBaseVertex.y - leftBaseVertex.x * leftBaseVertex1.y);
				float f2 = (rightBaseVertex1.y - rightBaseVertex.y) * v.p.x + (rightBaseVertex.x - rightBaseVertex1.x) * v.p.y + (rightBaseVertex1.x * rightBaseVertex.y - rightBaseVertex.x * rightBaseVertex1.y);
				if (f1 * f2 <= 0)
				{
					// Check it's ahead of the beam
					if (wp::MathsUtils::pointSideOnLine(v.p, leftBaseVertex, rightBaseVertex) != wp::MathsUtils::Side::Right)
					{
						// Get distance
						float distSq = v.p.distanceToLineSq(leftBaseVertex, rightBaseVertex);
						if ((distSq - 1.0f) <= lengthSq)
						{
							// Shoot rays down, if they intersect anything, discard them
							Vector2 start = v.p; 
							Vector2 end = v.p - beamDir * (v.p.distanceToLine(leftBaseVertex, rightBaseVertex) + MathsUtils::Epsilon);

							if (!lineIntersectsMesh(start, end, -beamDir, v))
							{
								// Get intersection with beam base
								wp::LineHit hit;
								wp::MathsUtils::rayRayIntersection(start, -beamDir, leftBaseVertex, -beamPerp, &hit);
								Vector2 hitPos = hit.getPosition();

								BeamShard leftShard, rightShard;
								leftShard.nearVertex = hitPos;
								leftShard.farVertex = v.p;
								leftShard.u = leftVertexMaxProjected.invLerp(hitPos, rightVertexMaxProjected);
								leftShard.v = leftShard.nearVertex.distanceTo(leftShard.farVertex) / length;
								
								// Check if it is glancing, and if so, add a line to the side of it
								Edge const& e0 = mMesh.edges[v.e[0]];
								Edge const& e1 = mMesh.edges[v.e[1]];

								float a0 = beamDir.minimumAngleTo(e0.n);
								float a1 = beamDir.minimumAngleTo(e1.n);

								bool leftGlance = LTE_EPSILON(a0, 90.0f) && a1 > 90;
								bool rightGlance = a0 > 90 && LTE_EPSILON(a1, 90.0f);
								if (leftGlance || rightGlance)
								{
									MathsUtils::rayRayIntersection(v.p, -beamDir, leftBaseVertex, -beamPerp, &hit);
									Vector2 nearHit = hit.getPosition();

									end = nearHit + beamDir * length;
									Vector2 hitGlance = projectLineThroughMesh(v.p, end, &v);

									rightShard.nearVertex = nearHit;
									rightShard.farVertex = hitGlance;
									rightShard.u = leftVertexMaxProjected.invLerp(nearHit, rightVertexMaxProjected);
									rightShard.v = rightShard.nearVertex.distanceTo(rightShard.farVertex) / length;
									
									if (leftGlance)
									{
										shards.push_back(leftShard);
										shards.push_back(rightShard);
									}
									else
									{
										shards.push_back(rightShard);
										shards.push_back(leftShard);
									}
								}
								else
								{
									shards.push_back(leftShard);
								}
							}
						}
					}
				}
			}

			if (rightFull)
			{
				Vector2 rightExtent = projectLineThroughMesh(rightBaseVertex, rightVertexMaxExtension);

				workShard.nearVertex = rightBaseVertex;
				workShard.farVertex = rightExtent;
				workShard.u = leftVertexMaxProjected.invLerp(rightBaseVertex, rightVertexMaxProjected);
				workShard.v = rightBaseVertex.invLerp(rightExtent, rightVertexMaxExtension);
				shards.push_back(workShard);
			}

			// Sort by distance of first point from left
			sort(shards.begin(), shards.end(), [leftBaseVertex](BeamShard const& a, BeamShard const& b)
			{
				return leftBaseVertex.distanceToSq(a.nearVertex) < leftBaseVertex.distanceToSq(b.nearVertex);
			});

			return shards;
		}

	} // firepower
} // WP_NAMESPACE