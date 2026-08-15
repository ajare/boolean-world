#include <mapbox/earcut.hpp>

#include "core/WorldData.h"
#include "core/Clipper.h"

namespace bw
{
	namespace core
	{

		using namespace std;

		WorldData::WorldData()
			: WorldData({}, -1.0f, {}, {}, {}, {}, {}, {}, 0)
		{
		}

		WorldData::WorldData(wp::BoundingBox const& extents)
			: WorldData(extents, (float)(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), {}, {}, {}, {}, {}, {}, 0)
		{
		}

		WorldData::WorldData(WorldDataDetail const& detail, float gridSize)
			: mDetail(detail)
			, mClippedPolygonLookupGrid(nullptr)
			, mTriangleLookupGrid(nullptr)
		{
			if (gridSize > 0.0f)
			{
				createGeometryAccelerationGrids(gridSize);
				setClippedPolygonData(detail.arrangementPolygons, detail.clipStats);
			}

			mStats.clip = detail.clipStats;
			mStats.prim = detail.primStats;
		}

		WorldData::WorldData(wp::BoundingBox const& extents, float gridSize, vector<ClippedPolygon> const& borderPolygons, vector<ClippedPolygon> const& clippedPolygons, vector<WorldVertexData> const& vertexData, graph::PolygonGraph const& graph, ClipStats const& clipStats, PrimitiveProcessingStats const& primStats, frame_number_type frameNumber)
			: WorldData({ extents, borderPolygons, clippedPolygons, vertexData, graph, clipStats, primStats, frameNumber }, gridSize)
		{
		}

		WorldData::~WorldData()
		{
			delete mClippedPolygonLookupGrid;
			delete mTriangleLookupGrid;
		}

		WorldData::WorldData(WorldData const& other)
			: mClippedPolygonLookupGrid(nullptr)
			, mTriangleLookupGrid(nullptr) 
		{
			copyFrom(other);
		}

		WorldData::WorldData(WorldData&& other) noexcept
			: mClippedPolygonLookupGrid(nullptr)
			, mTriangleLookupGrid(nullptr)
		{
			moveFrom(other);
		}

		WorldData& WorldData::operator=(WorldData const& other)
		{
			copyFrom(other);

			return *this;
		}

		WorldData& WorldData::operator=(WorldData&& other) noexcept
		{
			moveFrom(other);

			return *this;
		}

		void WorldData::copyFrom(WorldData const& other)
		{
			mDetail = other.mDetail;
			mStats = other.mStats;
			mTriangulation = other.mTriangulation;

			if (other.mClippedPolygonLookupGrid)
			{
				delete mClippedPolygonLookupGrid;

				mClippedPolygonLookupGrid = new wp::AccelerationGrid(
					other.mClippedPolygonLookupGrid->getOffset(),
					other.mClippedPolygonLookupGrid->getSize(),
					other.mClippedPolygonLookupGrid->getCellDimensionX(),
					other.mClippedPolygonLookupGrid->getCellDimensionY(),
					0.0f);

				auto numPolygons = (uint32_t)mDetail.borderPolygons.size();

				for (uint32_t i = 0; i < numPolygons; ++i)
				{
					mClippedPolygonLookupGrid->addItem(i, mDetail.borderPolygons[i].bounds);
				}
			}
			else
			{
				mClippedPolygonLookupGrid = nullptr;
			}

			if (other.mTriangleLookupGrid)
			{
				delete mTriangleLookupGrid;

				mTriangleLookupGrid = new wp::AccelerationGrid(
					other.mTriangleLookupGrid->getOffset(),
					other.mTriangleLookupGrid->getSize(),
					other.mTriangleLookupGrid->getCellDimensionX(),
					other.mTriangleLookupGrid->getCellDimensionY(),
					0.0f);

				auto numTriangles = (uint32_t)mTriangulation.tris.size();

				for (uint32_t i = 0; i < numTriangles; ++i)
				{
					mTriangleLookupGrid->addItem(i, mTriangulation.tris[i].bounds);
				}
			}
			else
			{
				mTriangleLookupGrid = nullptr;
			}
		}

		void WorldData::moveFrom(WorldData& other)
		{
			mDetail = move(other.mDetail);
			mTriangulation = move(other.mTriangulation);
			mStats = other.mStats;
			
			delete mClippedPolygonLookupGrid;
			mClippedPolygonLookupGrid = other.mClippedPolygonLookupGrid;
			other.mClippedPolygonLookupGrid = nullptr;

			delete mTriangleLookupGrid;
			mTriangleLookupGrid = other.mTriangleLookupGrid;
			other.mTriangleLookupGrid = nullptr;
		}

		void WorldData::setClippedPolygonData(vector<ClippedPolygon> const& clippedPolygons, ClipStats const& clipStats)
		{
			mClippedPolygonLookupGrid->clear();

			auto numPolygons = clippedPolygons.size();

			for (uint32_t i = 0; i < numPolygons; ++i)
			{
				mClippedPolygonLookupGrid->addItem(i, clippedPolygons[i].bounds);
			}

			// Set stats
			mStats.clip = clipStats;
		}

		wp::AccelerationGrid* WorldData::createLookupGrid(float cellSizeX, float cellSizeY)
		{
			wp::Vector2 minExtent, maxExtent;

			mDetail.extents.getExtents(minExtent, maxExtent);

			auto worldOffset = minExtent;
			auto worldSize = maxExtent - minExtent;

			int dimsX = max(1, (int)(worldSize.x / cellSizeX));
			int dimsY = max(1, (int)(worldSize.y / cellSizeY));

			return new wp::AccelerationGrid(worldOffset, worldSize, dimsX, dimsY, 0.0f);
		}

		void WorldData::createGeometryAccelerationGrids(float targetCellSize)
		{
			if (!mTriangleLookupGrid)
			{
				mTriangleLookupGrid = createLookupGrid(targetCellSize, targetCellSize);
			}

			if (!mClippedPolygonLookupGrid)
			{
				mClippedPolygonLookupGrid = createLookupGrid(targetCellSize, targetCellSize);
			}
		}

		wp::BoundingBox const& WorldData::getExtents() const
		{
			return mDetail.extents;
		}

		vector<ClippedPolygon> const& WorldData::getBorderPolygons() const
		{
			return mDetail.borderPolygons;
		}

		vector<ClippedPolygon> const& WorldData::getArrangementPolygons() const
		{
			return mDetail.arrangementPolygons;
		}

		Triangulation const& WorldData::getTriangulation() const
		{
			return mTriangulation;
		}

		vector<WorldVertexData> const& WorldData::getVertexData() const
		{
			return mDetail.vertexData;
		}

		WorldVertexData const& WorldData::getVertexData(uint32_t index) const
		{
			return mDetail.vertexData[index];
		}

		graph::PolygonGraph const& WorldData::getGraph() const
		{
			return mDetail.graph;
		}

		ClipStats const& WorldData::getClipStats() const
		{
			return mDetail.clipStats;
		}

		Stats const& WorldData::getStats() const
		{
			return mStats;
		}

		frame_number_type WorldData::getFrameNumber() const
		{
			return mDetail.frameNumber;
		}

		uint32_t WorldData::triangulate(World const* world)
		{
			mTriangleLookupGrid->clear();
			Triangulator triangulator(world, false, false, true, mTriangleLookupGrid);

			mStats.tri = triangulator.execute(mDetail.arrangementPolygons);
			
			mTriangulation = triangulator.getTriangulation();
			return (uint32_t)mDetail.arrangementPolygons.size();
		}

		bool WorldData::_pointInPolygon(wp::Vector2 const& pos, ClippedPolygon const& polygon) const
		{
			// https://alienryderflex.com/polygon/
			auto numVertices = (uint32_t)polygon.vertices.size();

			uint32_t i, j = numVertices - 1;
			bool oddNodes = false;
			auto const& v = polygon.vertices;

			for (i = 0; i < numVertices; i++)
			{
				auto v0 = v[i].p;
				auto v1 = v[j].p;

				if ((v0.y < pos.y && v1.y >= pos.y || v1.y < pos.y && v0.y >= pos.y) &&
					(v0.x <= pos.x || v1.x <= pos.x))
				{
					oddNodes ^= (v0.x + (pos.y - v0.y) / (v1.y - v0.y) * (v1.x - v0.x) < pos.x);
				}

				j = i;
			}

			return oddNodes;
		}

		int32_t WorldData::pointInTriangle(wp::Vector2 const& pos) const
		{
			int cellX, cellY;
			mTriangleLookupGrid->getContainingCell(true, pos.x, pos.y, cellX, cellY);

			if (cellX >= 0 && cellY >= 0)
			{
				auto indices = mTriangleLookupGrid->_getCellItems(cellX, cellY);

				for (auto index : indices)
				{
					auto const& tri = mTriangulation.tris[index];

					if (wp::MathsUtils::pointInTriangle(pos, tri.v[0].p, tri.v[1].p, tri.v[2].p))
					{
						return (int32_t)index;
					}
				}
			}

			return -1;
		}

		int32_t WorldData::pointInPolygon(wp::Vector2 const& pos) const
		{
			// The mDetail.polygons list is an ordered list of N polygons, and after each polygon is M holes.  
			// Eg. P,P,H,P,H,H,P -> 4 polygons, with 2nd having one hole and 3rd having two.

			auto numPolygons = (uint32_t)mDetail.borderPolygons.size();

			uint32_t i = 0;
			while (i < numPolygons)
			{
				auto const& polygon = mDetail.borderPolygons[i];

				// Find the polygon's holes, if any
				uint32_t next_i = i + 1;

				for (; next_i < numPolygons; ++next_i)
				{
					if (!mDetail.borderPolygons[next_i].isHole)
					{
						break;
					}
				}

				// Polies i+1 to j are holes, so check them first
				for (uint32_t j = i + 1; j < next_i; ++j)
				{
					if (_pointInPolygon(pos, mDetail.borderPolygons[j]))
					{
						return -1;
					}
				}

				// Now check the actual polygon
				if (_pointInPolygon(pos, mDetail.borderPolygons[i]))
				{
					return i;
				}

				// Next polygon
				i = next_i;
			}

			return -1;
		}

		bool WorldData::getContainingPolygon(wp::Vector2 const& pos, ClippedPolygon const** polygon) const
		{
			auto index = pointInPolygon(pos);
			if (index != -1)
			{
				*polygon = &mDetail.borderPolygons[index];
				return true;
			}
			else
			{
				*polygon = nullptr;
				return false;
			}
		}

		bool WorldData::getContainingTriangle(wp::Vector2 const& pos, Triangulation::Triangle const** triangle) const
		{
			auto index = mTriangulation.getContainingTriangleIndex(pos);
			if (index >= 0)
			{
				*triangle = &mTriangulation.tris[index];
				return true;
			}
			else
			{
				*triangle = nullptr;
				return false;
			}
		}
		
		uint32_t WorldData::getContainingTrianglePrimitiveIndex(wp::Vector2 const& pos) const
		{
			auto triIndex = mTriangulation.getContainingTriangleIndex(pos);

			return triIndex >= 0 ? mTriangulation.tris[triIndex].primitiveIndex : ~0u;
		}

		int32_t WorldData::getNearestWorldVertexIndex(wp::Vector2 const& pos, float radius) const
		{
			float r2 = radius * radius;
			float closestD2 = 1e10f;
			uint32_t closestIndex{ ~0u };

			for (auto const& polygon : mDetail.borderPolygons)
			{
				for (auto const& vertex : polygon.vertices)
				{
					wp::Vector2 vp{ (float)vertex.p.x, (float)vertex.p.y };
					float d2 = vp.distanceToSq(pos);

					if (d2 <= r2 && d2 < closestD2)
					{
						closestD2 = d2;
						closestIndex = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(vertex.z);
					}
				}
			}

			return (int32_t)closestIndex;
		}

		int32_t WorldData::circleIntersectsBorder(wp::Vector2 const& pos, float radius) const
		{
			auto bounds = wp::BoundingCircle(pos, radius);

			auto polygonIndices = mClippedPolygonLookupGrid->getCandidateItemsInBoundingArea(bounds);

			for (auto const& polygonIndex : polygonIndices)
			{
				auto const& polygon = mDetail.borderPolygons[polygonIndex];
				auto const& v = polygon.vertices;
				auto nv = (uint32_t)v.size();

				// Check if the distance to any lines is less than the radius
				for (uint32_t i = 0; i < nv; ++i)
				{
					uint32_t j = (i + 1) % nv;

					if (pos.distanceToLine(v[i].p, v[j].p) <= radius)
					{
						return (int32_t)polygonIndex;
					}
				}
			}

			return -1;
		}

		float WorldData::getNearestBorderDistance(wp::Vector2 const& pos) const
		{
			float minDist{ 1e10f };
			for (auto const& polygon : mDetail.borderPolygons)
			{
				auto const& v = polygon.vertices;
				auto nv = (uint32_t)v.size();

				// Check if the distance to any lines is less than the radius
				for (uint32_t i = 0; i < nv; ++i)
				{
					uint32_t j = (i + 1) % nv;

					auto distToLine = pos.distanceToLine(v[i].p, v[j].p);
					minDist = min(minDist, distToLine);
				}
			}

			return minDist;
		}

		uint32_t WorldData::getNumInterpolatedVertices() const
		{
			return 0;
		}

	} // core
} // bw