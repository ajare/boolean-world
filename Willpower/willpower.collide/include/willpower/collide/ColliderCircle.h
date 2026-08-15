#pragma once

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingCircle.h"

#include "willpower/collide/Platform.h"
#include "willpower/collide/Collider.h"

namespace WP_NAMESPACE
{
	namespace collide
	{

		class WP_COLLIDE_API ColliderCircle : public Collider
		{
			BoundingCircle mObject;

		public:

			ColliderCircle(float x, float y, float radius);

			ColliderCircle(Vector2 const& position, float radius);

			ColliderCircle(BoundingCircle const& bounds);

			void _setPosition(Vector2 const& position);

			BoundingCircle const& getObject() const;

			bool pointInside(float x, float y) const;

			bool sweepAgainstLine(Vector2 const& target, Vector2 const& linev0, Vector2 const& linev1, float* t) const;

			bool intersectsLine(Vector2 const& linev0, Vector2 const& linev1) const;
		};

	} // collide
} // WP_NAMESPACE

