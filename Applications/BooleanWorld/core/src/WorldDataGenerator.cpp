#include "core/WorldDataGenerator.h"
#include "core/World.h"
#include "core/Clipper.h"
#include "core/ClipperUtils.h"
#include "core/ClipperDefines.h"


namespace bw
{
	namespace core
	{
		using namespace std;

		WorldDataGenerator::WorldDataGenerator()
			: mCanClipPrimitives(true)
			, mActiveLayer(0)
			, mFlags(0)
			, mViewerPosition{0.0f, 0.0f}
			, mViewerAngle(0.0f)
			, mViewerViewDistance(256.0f)
			, mViewFOV(60.0f)
			, mBroadPhaseCulling(BroadPhaseCulling::None)
			, mNarrowPhaseCulling(NarrowPhaseCulling::None)
		{
		}

		WorldDataGenerator::~WorldDataGenerator()
		{
		}

		WorldDataGenerator::WorldDataGenerator(WorldDataGenerator const& other)
		{
			copyFrom(other);
		}

		WorldDataGenerator& WorldDataGenerator::operator=(WorldDataGenerator const& other)
		{
			copyFrom(other);
			return *this;
		}

		void WorldDataGenerator::copyFrom(WorldDataGenerator const& other)
		{
			mCanClipPrimitives = other.mCanClipPrimitives;
			mActiveLayer = other.mActiveLayer;
			mFlags = other.mFlags;
			mViewerPosition = other.mViewerPosition;
			mViewerExtent = other.mViewerExtent;
			mViewerAngle = other.mViewerAngle;
			mViewerViewDistance = other.mViewerViewDistance;
			mViewFOV = other.mViewFOV;
			mBroadPhaseCulling = other.mBroadPhaseCulling;
			mNarrowPhaseCulling = other.mNarrowPhaseCulling;
		}

		void WorldDataGenerator::setFlags(uint32_t flags)
		{
			mFlags = flags;
		}

		uint32_t WorldDataGenerator::getFlags() const
		{
			return mFlags;
		}

		bool WorldDataGenerator::flagSet(uint32_t flag) const
		{
			return (mFlags & flag) != 0;
		}

		WorldDataGenerator::BroadPhaseCulling WorldDataGenerator::getBroadPhaseCulling() const
		{
			return mBroadPhaseCulling;
		}

		void WorldDataGenerator::setBroadPhaseCulling(BroadPhaseCulling culling)
		{
			mBroadPhaseCulling = culling;
		}

		WorldDataGenerator::NarrowPhaseCulling WorldDataGenerator::getNarrowPhaseCulling() const
		{
			return mNarrowPhaseCulling;
		}

		void WorldDataGenerator::setNarrowPhaseCulling(NarrowPhaseCulling culling)
		{
			mNarrowPhaseCulling = culling;
		}

		void WorldDataGenerator::setView(wp::Vector2 const& pos, wp::Vector2 const& extent, float angle, float distance, float fov)
		{
			mViewerPosition = pos;
			mViewerExtent = extent;
			mViewerAngle = angle;
			mViewerViewDistance = distance;
			mViewFOV = fov;
		}

		vector<wp::Vector2> WorldDataGenerator::getViewVertices(wp::Vector2 const& pos, float viewAngle, float viewDist, float fov)
		{
			auto fov2 = fov * 0.5f;

			// Increase the view distance a little, to allow some extra time for potentially-asynchronous clipping to
			// be performed
			viewDist *= 1.1f;

			// Re-calculate view distance so that triangle height is view distance
			viewDist /= cosf(WP_DEGTORAD(fov2));

			auto p = pos;
			auto v0 = p + wp::Vector2::fromAngle(viewAngle - fov2, wp::Clockwise) * viewDist;
			auto v1 = p + wp::Vector2::fromAngle(viewAngle + fov2, wp::Clockwise) * viewDist;

			return { p, v0, v1 };
		}

		vector<wp::Vector2> WorldDataGenerator::getViewVertices() const
		{
			return getViewVertices(mViewerPosition, mViewerAngle, mViewerViewDistance, mViewFOV);
		}

		bool WorldDataGenerator::primitiveInView(Primitive const* primitive, vector<wp::Vector2> const& viewVertices)
		{
			wp::Vector2 boundsMin, boundsMax;
			auto const& bounds = primitive->getBounds();

			bounds.getExtents(boundsMin, boundsMax);

			return wp::MathsUtils::boxIntersectsTriangle(boundsMin, boundsMax, viewVertices[0], viewVertices[1], viewVertices[2]);
		}

		bool WorldDataGenerator::primitiveInView(Primitive const* primitive) const
		{
			return primitiveInView(primitive, getViewVertices());
		}

		vector<Primitive*> WorldDataGenerator::getPrimitives(World const* world, uint8_t layer) const
		{
			vector<Primitive*> primitives;

			// Broad phase
			switch (getBroadPhaseCulling())
			{
			case BroadPhaseCulling::None:
				primitives = world->getPrimitives();
				break;

			case BroadPhaseCulling::Box:
				primitives = world->findPrimitives(wp::BoundingBox(mViewerPosition - mViewerExtent / 2, mViewerExtent));
				break;

			case BroadPhaseCulling::Circle:
				primitives = world->findPrimitives(wp::BoundingCircle(mViewerPosition, mViewerViewDistance));
				break;

			default:
				throw CoreException("Unhandled WorldDataGenerator::BroadPhaseCulling value");
			}

			// Narrow phase
			auto viewVerts = getViewVertices();

			uint32_t curPrimCount{ 0 }, pc = (uint32_t)primitives.size();

			for (uint32_t i = 0; i < pc; ++i)
			{
				auto p = primitives[i];
				auto pLayer = p->getLayer();

				// Ignore primitives on a different layer, unless the Primitive is the "all" layer,
				// or we are on the "all" layer
				if (pLayer != layer && layer != BW_LAYER_ALL && pLayer != BW_LAYER_ALL)
				{
					continue;
				}

				// Check bounds
				wp::Vector2 boundsMin, boundsMax;

				auto const& primBounds = primitives[i]->getBounds();

				primBounds.getExtents(boundsMin, boundsMax);

				switch (getNarrowPhaseCulling())
				{
				case NarrowPhaseCulling::None:
					break;

				case NarrowPhaseCulling::Circle:
					if (!wp::MathsUtils::boxIntersectsCircle(boundsMin, boundsMax, mViewerPosition, mViewerViewDistance))
					{
						continue;
					}
					break;

				case NarrowPhaseCulling::Cone:
					if (!wp::MathsUtils::boxIntersectsTriangle(boundsMin, boundsMax, viewVerts[0], viewVerts[1], viewVerts[2]))
					{
						continue;
					}
					break;

				default:
					throw CoreException("Unhandled WorldDataGenerator::NarrowPhaseCulling value");
				}
				
				// Keep this Primitive
				if (i != curPrimCount)
				{
					primitives[curPrimCount] = primitives[i];
				}

				curPrimCount++;
			}

			// 3. Get the Primitive(s) the Player is in, and recursively test their bounds against the others,
			//    building up as minimal a set as we can, without actually clipping
			//    To do this, check all Primitives in [0, curPrimCount) against those in [curPrimCount,<size>)
			//    If one is found, move it into the lower half
			/*
			uint32_t newPrimCount{ 0 };

			for (uint32_t i = 0; i < curPrimCount; ++i)
			{
				auto const& primBounds = primitives[i]->getBounds();

				if (primBounds.pointInside(mViewerPosition))
				{
					if (i != newPrimCount)
					{
						primitives[newPrimCount] = primitives[i];
					}

					newPrimCount++;
				}
			}

			// At this point, up to newPrimCount are the Primitives that the Player is inside
			for (uint32_t i = 0; i < newPrimCount; ++i)
			{
				for (uint32_t j = newPrimCount; j < curPrimCount; ++j)
				{
					auto const& primBounds0 = primitives[i]->getBounds();
					auto const& primBounds1 = primitives[j]->getBounds();

					if (primBounds0.intersectsBoundingObject(&primBounds1))
					{
						auto tempPrim = primitives[j];
						primitives[j] = primitives[newPrimCount];
						primitives[newPrimCount++] = tempPrim;
					}
				}
			}
			*/

			// Remove all the Primitives we no longer are interested in
			uint32_t removeCount = (uint32_t)primitives.size() - curPrimCount;

			while (removeCount--)
			{
				primitives.pop_back();
			}


			return primitives;
		}

		WorldDataClipResults WorldDataGenerator::clipPrimitives(vector<Primitive*> const& primitives, World const* world, bool clipInputPrimitives) const
		{
			if (primitives.empty())
			{
				return {};
			}

			// Clip Primitives together to form main mesh
			frame_number_type worldDataFrameNumber;
			Clipper borderClipper(world->getBorderVertexData(&worldDataFrameNumber), {}, 
				world,
				BW_CLIPPER_SET_PRIMITIVE |
				BW_CLIPPER_GEN_INTER_ON_UNION
			);

			auto arrangementPolygons = borderClipper.clipToClipper2Polygons(primitives);
			auto const& vertexData = borderClipper.getClippedWorldVertexData();

			return {
				borderClipper.getBorderPolygons(),
				arrangementPolygons,
				vertexData,
				borderClipper.getArrangementGraph(),
				worldDataFrameNumber,
				borderClipper.getStats()
			};
		}

		bool WorldDataGenerator::canClipPrimitives() const
		{
			return mCanClipPrimitives;
		}

		void WorldDataGenerator::setActiveLayer(uint8_t layer)
		{
			mActiveLayer = layer;
		}

		uint8_t WorldDataGenerator::getActiveLayer() const
		{
			return mActiveLayer;
		}

		void WorldDataGenerator::handleEvents(uint32_t events)
		{
			BW_UNUSED(events);
		}

		void WorldDataGenerator::update(float frameTime, WorldUpdateData const& data, wp::Vector2 const& viewSize, uint32_t events)
		{
			mCanClipPrimitives = !data.entityMoved && !data.entityTurned;
			mActiveLayer = data.activeLayer;

			setView(data.entityPosition, viewSize, data.entityAngle, data.entityViewDist, data.entityFov);
			handleEvents(events);
		}

	} // bw
} // core
