#pragma once

#include <vector>
#include <functional>

#include "willpower/common/Vector2.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/AreaQuery.h"
#include "willpower/firepower/Mesh.h"
#include "willpower/firepower/ControlSetting.h"
#include "willpower/firepower/BeamShard.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		class MeshCollisionManager;

		class WP_FIREPOWER_API Beam : public AreaQuery
		{
			float mHeadOffset, mBodyOffset, mBaseOffset;

		public:

			enum class TexCoordOptions
			{
				Unitised,
				Scaled
			};

			enum class SectionOptions
			{
				Scale9,
				Scale3,
				Scale2,
				Single
			};

		private:

			int mType;

			bool mAlive;

			Vector2 mPosition;

			float mAcceleration;

			float mSpeed;

			ControlFunction mGetAccelerationFn;

			ControlFunction mGetSpeedFn;

			float mWidth, mMaxLength, mLength;

			float mAngle;

			// Render data
			std::vector<BeamShard> mShards;

		public:

			Beam();

			void initialise(int type, Vector2 const& position, float length, float width, float angle, ControlSetting accelValue, ControlSetting speedValue);

			int getType() const;

			void setAlive(bool alive);

			void setPosition(Vector2 const& position);

			wp::Vector2 const& getPosition() const;

			void setWidth(float width);

			float getWidth() const;
			
			float getLength() const;

			void setAngle(float angle);

			float getAngle() const;

			std::vector<BeamShard> const& getShards() const;

			bool update(MeshCollisionManager const* meshCollisionMgr, float frameTime);
		};

	} // firepower
} // WP_NAMESPACE

