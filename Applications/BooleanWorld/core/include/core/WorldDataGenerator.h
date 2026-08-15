#pragma once

#include <functional>
#include <map>

#include "core/WorldData.h"
#include "core/Primitive.h"
#include "core/Clipper2Polygon.h"
#include "core/WorldUpdateData.h"
#include "core/PolygonGraph.h"
#include "core/Stats.h"


namespace bw
{
	namespace core
	{
		class World;

		struct WorldDataClipResults
		{
			std::vector<Clipper2Polygon> borderPolygons;
			std::vector<Clipper2Polygon> arrangementPolygons;
			std::vector<WorldVertexData> borderVertexData;
			graph::PolygonGraph graph;
			frame_number_type vertexDataFrameNumber{ 0 };
			ClipStats stats;
		};

		class WorldDataGenerator
		{
		public:

			enum struct BroadPhaseCulling
			{
				None,
				Circle,
				Box
			};

			enum struct NarrowPhaseCulling
			{
				None,
				Circle,
				Cone
			};

			struct SortPrimitivesByPriority
			{
				bool operator()(Primitive const* a, Primitive const* b)
				{
					return a->getPriority() < b->getPriority();
				}
			};

		private:

			bool mCanClipPrimitives;

			uint8_t mActiveLayer;

			uint32_t mFlags;

			BroadPhaseCulling mBroadPhaseCulling;

			NarrowPhaseCulling mNarrowPhaseCulling;

		protected:

			wp::Vector2 mViewerPosition, mViewerExtent;

			float mViewerAngle, mViewerViewDistance, mViewFOV;

		private:

			virtual void handleEvents(uint32_t events);

		protected:

			void copyFrom(WorldDataGenerator const& other);

			std::vector<Primitive*> getPrimitives(World const* world, uint8_t layer) const;

			WorldDataClipResults clipPrimitives(std::vector<Primitive*> const& primitives, World const* world, bool clipInputPrimitives) const;

			bool canClipPrimitives() const;

		public:

			WorldDataGenerator();

			virtual ~WorldDataGenerator();

			WorldDataGenerator(WorldDataGenerator const& other);

			WorldDataGenerator& operator=(WorldDataGenerator const& other);

			virtual WorldDataGenerator* copy() = 0;

			virtual WorldData getWorldData(World const* world) = 0;

			void setFlags(uint32_t flags);

			uint32_t getFlags() const;

			bool flagSet(uint32_t flag) const;

			void setActiveLayer(uint8_t layer);

			uint8_t getActiveLayer() const;

			BroadPhaseCulling getBroadPhaseCulling() const;

			void setBroadPhaseCulling(BroadPhaseCulling culling);

			NarrowPhaseCulling getNarrowPhaseCulling() const;
			
			void setNarrowPhaseCulling(NarrowPhaseCulling culling);

			static std::vector<wp::Vector2> getViewVertices(wp::Vector2 const& pos, float viewAngle, float viewDist, float fov);

			std::vector<wp::Vector2> getViewVertices() const;

			static bool primitiveInView(Primitive const* primitive, std::vector<wp::Vector2> const& viewVertices);

			bool primitiveInView(Primitive const* primitive) const;

			void setView(wp::Vector2 const& pos, wp::Vector2 const& extent, float angle, float distance, float fov);

			void update(float frameTime, WorldUpdateData const& data, wp::Vector2 const& viewSize, uint32_t events);

			virtual void generate(World const* world, NarrowPhaseCulling culling, bool regetPrimitives) = 0;
		};

		typedef std::function<WorldDataGenerator* (wp::Vector2, int, int, float)> WorldDataGeneratorFactory;

	} // bw
} // core
