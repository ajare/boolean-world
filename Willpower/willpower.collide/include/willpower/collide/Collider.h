#pragma once

#include <functional>

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"

#include "willpower/collide/Platform.h"
#include "willpower/collide/StaticLine.h"
#include "willpower/collide/SweepResult.h"

namespace WP_NAMESPACE
{
	namespace collide
	{

		class Simulation;

		class WP_COLLIDE_API Collider
		{
			friend class Simulation;

		public:

			typedef std::function<bool(SweepResult*, StaticLine const&, float, void*)> HitLineCallback;

		public:

			enum Type
			{
				Circle,
				AABB,
				MAX_TYPES = 8
			};

		private:

			int32_t mIndex;

			Type mType;

			BoundingBox mBounds;

			Vector2 mMovement;

			bool mConsumeMovement;

			HitLineCallback mHitLineCallback;
			
		private:

			void _setIndex(int32_t index);

			void _consumeMovement(Vector2 const& movement);

		protected:

			void setBounds(BoundingBox const& box);

			void setBounds(float x, float y, float width, float height);

			void setBounds(Vector2 const& position, Vector2 const& size);

		public:

			explicit Collider(Type type);

			virtual ~Collider() = default;

			int32_t getIndex() const;

			Type getType() const;

			Vector2 const& getCentre() const;

			virtual void _setPosition(Vector2 const& position) = 0;

			BoundingBox const& getBounds() const;

			void setHitLineCallback(HitLineCallback callback);

			bool fireHitLineCallback(SweepResult* result, StaticLine const& line, float t, void* userObj) const;

			virtual bool pointInside(float x, float y) const = 0;

			virtual bool pointInside(Vector2 const& point) const;

			void setMovement(float x, float y);

			void setMovement(Vector2 const& movement);

			Vector2 const& getMovement() const;

			void setConsumeMovement(bool consume);

			bool isMovementConsumed() const;

			virtual bool sweepAgainstLine(Vector2 const& target, Vector2 const& linev0, Vector2 const& linev1, float* t) const = 0;

			virtual bool intersectsLine(Vector2 const& linev0, Vector2 const& linev1) const = 0;

			static void checkCollision(Collider* a, Collider* b);
		};

	} // collide
} // WP_NAMESPACE

