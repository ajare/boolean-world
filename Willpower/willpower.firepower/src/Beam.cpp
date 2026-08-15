#include <algorithm>

#include <utils/StringUtils.h>

#include "willpower/common/MathsUtils.h"

#include "willpower/firepower/Beam.h"
#include "willpower/firepower/MeshCollisionManager.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		using namespace std;

		Beam::Beam()
			: mAlive(false)
			, mPosition(Vector2::ZERO)
			, mAcceleration(0.0f)
			, mSpeed(0.0f)
			, mGetAccelerationFn({})
			, mGetSpeedFn({}), mWidth(0.0f)
			, mMaxLength(0.0f)
			, mLength(0.0f)
			, mAngle(0.0f)
		{
		}

		void Beam::initialise(int type, Vector2 const& position, float length, float width, float angle, ControlSetting accelValue, ControlSetting speedValue)
		{
			mType = type;
			mAlive = true;
			mPosition = position;
			mLength = 0;
			mMaxLength = length;
			mWidth = width;
			mAngle = angle;

			// Acceleration
			if (accelValue.option == ControlOptions::Function)
			{
				mGetAccelerationFn = accelValue.function;
				mAcceleration = 0.0f;
			}
			else if (accelValue.option == ControlOptions::Scalar)
			{
				mGetAccelerationFn = {};
				mAcceleration = accelValue.scalar;
			}
			else
			{
				mGetAccelerationFn = {};
				mAcceleration = 0.0f;
			}

			// Speed
			if (speedValue.option == ControlOptions::Function)
			{
				mGetSpeedFn = speedValue.function;
				mSpeed = 0.0f;
			}
			else if (speedValue.option == ControlOptions::Scalar)
			{
				mGetSpeedFn = {};
				mSpeed = speedValue.scalar;
			}
			else
			{
				mGetSpeedFn = {};
				mSpeed = 0.0f;
			}
		}

		int Beam::getType() const
		{
			return mType;
		}

		void Beam::setAlive(bool alive)
		{
			mAlive = alive;
		}

		void Beam::setPosition(Vector2 const& position)
		{
			mPosition = position;
		}

		Vector2 const& Beam::getPosition() const
		{
			return mPosition;
		}

		void Beam::setWidth(float width)
		{
			mWidth = width;
		}

		float Beam::getWidth() const
		{
			return mWidth;
		}
		
		float Beam::getLength() const
		{
			return mLength;
		}

		void Beam::setAngle(float angle)
		{
			mAngle = angle;
		}

		float Beam::getAngle() const
		{
			return mAngle;
		}

		vector<BeamShard> const& Beam::getShards() const
		{
			return mShards;
		}

		bool Beam::update(MeshCollisionManager const* meshCollisionMgr, float frameTime)
		{
			if (!mAlive)
			{
				return false;
			}

			// Update acceleration
			if (mGetAccelerationFn)
			{
				mAcceleration = mGetAccelerationFn(frameTime, nullptr);
			}

			// Update speed
			if (mGetSpeedFn)
			{
				mSpeed = mGetSpeedFn(frameTime, nullptr);
			}
			else
			{
				mSpeed += mAcceleration * frameTime;
			}

			if (mLength < mMaxLength)
			{
				mLength = min(mLength + mSpeed * frameTime, mMaxLength);
			}

			mShards = meshCollisionMgr->checkBeam(mPosition, mAngle, mWidth, mLength);
			
			bool alive = true;
			setAlive(alive);
			
			return alive;
		}
		
	} // firepower
} // WP_NAMESPACE

