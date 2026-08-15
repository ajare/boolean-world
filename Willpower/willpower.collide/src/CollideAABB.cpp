#include <cassert>

#include "willpower/common/MathsUtils.h"

#include "willpower/collide/ColliderAABB.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace collide
	{

		using namespace WP_NAMESPACE;

		ColliderAABB::ColliderAABB(float x, float y, float width, float height)
			: Collider(Type::AABB)
			, mObject(x, y, width, height)
		{
			setBounds(x, y, width, height);
		}

		ColliderAABB::ColliderAABB(Vector2 const& position, Vector2 const& size)
			: ColliderAABB(position.x, position.y, size.x, size.y)
		{
		}

		ColliderAABB::ColliderAABB(BoundingBox const& bounds)
			: Collider(Type::AABB)
			, mObject(bounds)
		{
			setBounds(bounds);
		}

		void ColliderAABB::_setPosition(Vector2 const& position)
		{
			mObject.centreOn(position);
			setBounds(mObject);
		}

		BoundingBox const& ColliderAABB::getObject() const
		{
			return mObject;
		}

		bool ColliderAABB::pointInside(float x, float y) const
		{
			return mObject.pointInside(x, y);
		}

		bool ColliderAABB::sweepAgainstLine(Vector2 const& target, Vector2 const& linev0, Vector2 const& linev1, float* t) const
		{
			return MathsUtils::sweepAABBAgainstLine(
				getCentre(),
				getObject().getHalfSize(),
				target,
				linev0,
				linev1,
				t);
		}

		bool ColliderAABB::intersectsLine(Vector2 const& linev0, Vector2 const& linev1) const
		{
			return MathsUtils::lineIntersectsBox(linev0, linev1, mObject.getMinExtent(), mObject.getMaxExtent()) != MathsUtils::LineIntersectionType::NotIntersecting;
		}

	} // collide
} //WP_NAMESPACE
