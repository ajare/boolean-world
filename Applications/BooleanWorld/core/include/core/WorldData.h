#pragma once

#include <vector>
#include <array>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/Timer.h>

#include "core/Platform.h"
#include "core/Clipper.h"
#include "core/Triangulator.h"
#include "core/Triangulation.h"
#include "core/WorldVertexData.h"
#include "core/PolygonGraph.h"
#include "core/Stats.h"


namespace bw
{
	namespace core
	{
		struct WorldDataDetail
		{
			wp::BoundingBox extents;
			std::vector<ClippedPolygon> borderPolygons;
			std::vector<ClippedPolygon> arrangementPolygons;
			std::vector<WorldVertexData> vertexData;
			graph::PolygonGraph graph;
			ClipStats clipStats;
			PrimitiveProcessingStats primStats;
			frame_number_type frameNumber;

		private:

			void copyFrom(WorldDataDetail const& other)
			{
				extents = other.extents;
				borderPolygons = other.borderPolygons;
				arrangementPolygons = other.arrangementPolygons;
				vertexData = other.vertexData;
				graph = other.graph;
				clipStats = other.clipStats;
				primStats = other.primStats;
				frameNumber = other.frameNumber;
			}

			void moveFrom(WorldDataDetail& other)
			{
				extents = other.extents;
				
				borderPolygons = std::move(other.borderPolygons);
				arrangementPolygons = std::move(other.arrangementPolygons);
				vertexData = std::move(other.vertexData);
				graph = std::move(other.graph);
				
				clipStats = other.clipStats;
				primStats = other.primStats;
				frameNumber = other.frameNumber;
			}

		public:

			WorldDataDetail()
				: frameNumber(0)
			{
			}

			WorldDataDetail(wp::BoundingBox const& extents_, std::vector<ClippedPolygon> const& borderPolygons_, std::vector<ClippedPolygon> const& arrangementPolygons_, std::vector<WorldVertexData> const& vertexData_, graph::PolygonGraph const& graph_, ClipStats const& clipStats_, PrimitiveProcessingStats const& primStats_, frame_number_type frameNumber_)
				: extents(extents_)
				, borderPolygons(borderPolygons_)
				, arrangementPolygons(arrangementPolygons_)
				, vertexData(vertexData_)
				, graph(graph_)
				, clipStats(clipStats_)
				, primStats(primStats_)
				, frameNumber(frameNumber_)
			{
			}

			WorldDataDetail(WorldDataDetail const& other)
			{
				copyFrom(other);
			}

			WorldDataDetail& operator=(WorldDataDetail const& other)
			{
				copyFrom(other);

				return *this;
			}
			
			WorldDataDetail(WorldDataDetail&& other) noexcept
			{
				moveFrom(other);
			}

			WorldDataDetail& operator=(WorldDataDetail&& other) noexcept
			{
				moveFrom(other);

				return *this;
			}
			
		};

		class BW_API WorldData
		{
			friend class World;
			friend class DefaultWorldDataGenerator;
			friend class DynamicWorldDataGenerator;

		private:

			WorldDataDetail mDetail;
			
			Triangulation mTriangulation;

			Stats mStats;

			wp::AccelerationGrid* mClippedPolygonLookupGrid;
			
			wp::AccelerationGrid* mTriangleLookupGrid;

		private:

			void copyFrom(WorldData const& other);

			void moveFrom(WorldData& other);

			wp::AccelerationGrid* createLookupGrid(float cellSizeX, float cellSizeY);

			void createGeometryAccelerationGrids(float targetCellSize);

			void setClippedPolygonData(std::vector<ClippedPolygon> const& clippedPolygons, ClipStats const& clipStats);

			bool _pointInPolygon(wp::Vector2 const& pos, ClippedPolygon const& polygon) const;

		protected:

			explicit WorldData(wp::BoundingBox const& extents);

			WorldData(WorldDataDetail const& detail, float gridSize);
			
			WorldData(wp::BoundingBox const& extents, float gridSize, std::vector<ClippedPolygon> const& borderPolygons, std::vector<ClippedPolygon> const& clippedPolygons, std::vector<WorldVertexData> const& vertexData, graph::PolygonGraph const& graph, ClipStats const& clipStats, PrimitiveProcessingStats const& primStats, frame_number_type frameNumber);

		public:

			WorldData();

			virtual ~WorldData();

			WorldData(WorldData const& other);

			WorldData(WorldData&& other) noexcept;

			WorldData& operator=(WorldData const& other);

			WorldData& operator=(WorldData&& other) noexcept;

			[[nodiscard]] wp::BoundingBox const& getExtents() const;

			[[nodiscard]] std::vector<ClippedPolygon> const& getBorderPolygons() const;

			[[nodiscard]] std::vector<ClippedPolygon> const& getArrangementPolygons() const;

			[[nodiscard]] Triangulation const& getTriangulation() const;

			[[nodiscard]] std::vector<WorldVertexData> const& getVertexData() const;

			[[nodiscard]] WorldVertexData const& getVertexData(uint32_t index) const;

			[[nodiscard]] graph::PolygonGraph const& getGraph() const;

			[[nodiscard]] ClipStats const& getClipStats() const;

			[[nodiscard]] Stats const& getStats() const;

			[[nodiscard]] frame_number_type getFrameNumber() const;

			uint32_t triangulate(World const* world);

			[[nodiscard]] int32_t pointInTriangle(wp::Vector2 const& pos) const;

			[[nodiscard]] int32_t pointInPolygon(wp::Vector2 const& pos) const;

			bool getContainingPolygon(wp::Vector2 const& pos, ClippedPolygon const** polygon) const;

			bool getContainingTriangle(wp::Vector2 const& pos, Triangulation::Triangle const** triangle) const;

			uint32_t getContainingTrianglePrimitiveIndex(wp::Vector2 const& pos) const;

			int32_t getNearestWorldVertexIndex(wp::Vector2 const& pos, float radius) const;

			[[nodiscard]] int32_t circleIntersectsBorder(wp::Vector2 const& pos, float radius) const;

			[[nodiscard]] float getNearestBorderDistance(wp::Vector2 const& pos) const;

			[[nodiscard]] uint32_t getNumInterpolatedVertices() const;

		};

	} // bw
} // core
