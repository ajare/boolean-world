#pragma once

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"

#include "willpower/collide/Platform.h"
#include "willpower/collide/Collider.h"

namespace WP_NAMESPACE
{
	namespace collide
	{

		class WP_COLLIDE_API ColliderAABB : public Collider
		{
			BoundingBox mObject;

		public:

			ColliderAABB(float x, float y, float width, float height);

			ColliderAABB(Vector2 const& position, Vector2 const& size);

			ColliderAABB(BoundingBox const& bounds);

			void _setPosition(Vector2 const& position);

			BoundingBox const& getObject() const;

			bool pointInside(float x, float y) const;

			bool sweepAgainstLine(Vector2 const& target, Vector2 const& linev0, Vector2 const& linev1, float* t) const;

			bool intersectsLine(Vector2 const& linev0, Vector2 const& linev1) const;
		};

	} // collide
} // WP_NAMESPACE

