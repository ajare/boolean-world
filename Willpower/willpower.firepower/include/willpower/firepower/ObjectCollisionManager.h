#pragma once

#include "willpower/common/DynamicAccelerationGrid.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/DynamicCollisionObject.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		class WP_FIREPOWER_API ObjectCollisionManager
		{
			DynamicAccelerationGrid* mGrid;

			std::vector<DynamicCollisionObject> mCollisionObjects;

			std::vector<uint32_t> mFreeObjectIndices;

		public:

			ObjectCollisionManager(Vector2 const& origin, Vector2 const& size, float cellSize, int initialCount, float maxObjectDimension);

			~ObjectCollisionManager();

			DynamicAccelerationGrid const* getGrid() const;

			DynamicAccelerationGrid* getGrid();

			void clearFrameCache();

			uint32_t addAABB(Vector2 const& minExtent, Vector2 const& maxExtent);

			void removeObject(uint32_t objectId);

			void onObjectMoved(uint32_t objectId, Vector2 const& minExtent, Vector2 const& maxExtent);

			int32_t checkBullet(Vector2 const& oldPosition, Vector2 const& newPosition, float radius, float checkTunnelling);
		};

	} // firepower
} // WP_NAMESPACW#pragma once
