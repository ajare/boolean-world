#include "willpower/common/Globals.h"

#include "willpower/firepower/ObjectCollisionManager.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		using namespace std;

		ObjectCollisionManager::ObjectCollisionManager(Vector2 const& origin, Vector2 const& size, float cellSize, int initialCount, float maxObjectDimension)
			: mGrid(nullptr)
		{
			int cellsX = (int)ceil(size.x / cellSize);
			int cellsY = (int)ceil(size.y / cellSize);
			
			Vector2 adjustedSize(cellsX * cellSize, cellsY * cellSize);
			Vector2 adjustedOrigin = origin - (adjustedSize - size) * 0.5f;

			mGrid = new DynamicAccelerationGrid(
				adjustedOrigin, 
				adjustedSize,
				cellsX, 
				cellsY,
				initialCount,
				maxObjectDimension
			);

			// Collision objects
			mCollisionObjects.resize(initialCount);

			for (int i = initialCount; i > 0;)
			{
				mFreeObjectIndices.push_back((uint32_t)--i);
			}
		}

		ObjectCollisionManager::~ObjectCollisionManager()
		{
			delete mGrid;
		}

		DynamicAccelerationGrid const* ObjectCollisionManager::getGrid() const
		{
			return mGrid;
		}

		DynamicAccelerationGrid* ObjectCollisionManager::getGrid()
		{
			return mGrid;
		}

		void ObjectCollisionManager::clearFrameCache()
		{
		}

		uint32_t ObjectCollisionManager::addAABB(Vector2 const& minExtent, Vector2 const& maxExtent)
		{
			if (mFreeObjectIndices.empty())
			{
				// Resize storage
				size_t oldSize = mCollisionObjects.size();
				mCollisionObjects.resize(oldSize * 2);

				int newSize = (int)mCollisionObjects.size();
				for (int i = newSize; i > (int)oldSize;)
				{
					mFreeObjectIndices.push_back((uint32_t)--i);
				}
			}

			uint32_t objectId = mFreeObjectIndices.back();
			mFreeObjectIndices.pop_back();

			BoundingBox bounds(minExtent, maxExtent - minExtent);

			// Set up mCollisionObjects[objectId]
			mCollisionObjects[objectId].origin = bounds.getPosition();
			mCollisionObjects[objectId].size = bounds.getSize();

			mGrid->addItem(objectId, bounds);
			return objectId;
		}

		void ObjectCollisionManager::removeObject(uint32_t objectId)
		{
			mGrid->removeItem(objectId);
			mFreeObjectIndices.push_back(objectId);
		}

		void ObjectCollisionManager::onObjectMoved(uint32_t objectId, Vector2 const& minExtent, Vector2 const& maxExtent)
		{
			BoundingBox bounds(minExtent, maxExtent - minExtent);
			
			mGrid->moveItem(objectId, bounds);

			mCollisionObjects[objectId].origin = bounds.getPosition();
			mCollisionObjects[objectId].size = bounds.getSize();
		}

		int32_t ObjectCollisionManager::checkBullet(Vector2 const& oldPosition, Vector2 const& newPosition, float radius, float checkTunnelling)
		{
			WP_UNUSED(oldPosition);
			WP_UNUSED(checkTunnelling);

			auto const& objectIds = mGrid->getCandidateItemsInBoundingArea(BoundingCircle(newPosition, radius));

			BoundingCircle bc(newPosition, radius);
			for (uint32_t objectId: objectIds)
			{
				auto const& collisionObj = mCollisionObjects[objectId];
				BoundingBox bb(collisionObj.origin, collisionObj.size);

				if (bb.intersectsBoundingObject(&bc))
				{
					return (uint32_t)objectId;
				}
			}

			return -1;
		}

	} // firepower
} // WP_NAMESPACE