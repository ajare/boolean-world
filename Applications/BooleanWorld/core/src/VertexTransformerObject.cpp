#include "core/VertexTransformerObject.h"
#include "core/Utils.h"
#include "core/Defines.h"


namespace bw
{
	namespace core
	{
		using namespace std;

		VertexTransformerObject::VertexTransformerObject()
			: mId(~0u)
			, mParent(nullptr)
			, mPosition(wp::Vector2::ZERO)
			, mTransformOffset(wp::Vector2::ZERO)
			, mOrientation(0.0f)
			, mPrevEntityPosition(wp::Vector2::ZERO)
			, mPrevEntityAngle(0.0f)
		{
		}

		VertexTransformerObject::VertexTransformerObject(VertexTransformerObject const& other)
		{
			copyFrom(other);
		}

		VertexTransformerObject& VertexTransformerObject::operator=(VertexTransformerObject const& other)
		{
			copyFrom(other);
			return *this;
		}

		bool VertexTransformerObject::childrenModified() const
		{
			return mVertexTransformer.isModified();
		}

		void VertexTransformerObject::copyFrom(VertexTransformerObject const& other)
		{
			mId = other.mId;
			mParent = other.mParent;
			mVertexTransformer = other.mVertexTransformer;
			mPosition = other.mPosition;
			mTransformOffset = other.mTransformOffset;
			mOrientation = other.mOrientation;
			mEye = other.mEye;
			mInputs = other.mInputs;
		}

		bool VertexTransformerObject::isStatic() const
		{
			return mVertexTransformer.isStatic();
		}

		void VertexTransformerObject::cacheStaticness(bool cache)
		{
			mVertexTransformer.cacheStaticness(cache);
		}

		void VertexTransformerObject::resetAnimatorCaptures()
		{
			mVertexTransformer.resetAnimatorCaptures();
		}

		void VertexTransformerObject::resetAnimator(VertexTransformer::Key key)
		{
			mVertexTransformer.resetAnimator(key);
		}

		void VertexTransformerObject::setId(uint32_t id)
		{
			mId = id;
		}

		uint32_t VertexTransformerObject::getId() const
		{
			return mId;
		}

		void VertexTransformerObject::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const
		{
			serializer->beginMap("vertexTransformerObject");
			{
				serializer->writeUint32("id", mId);
				serializer->writeInt32("parentId", mParent ? (int32_t)mParent->mId : -1);
				serializer->writeVector2("position", mPosition);
				serializer->writeVector2("transformOffset", mTransformOffset);
				serializer->writeFloat("orientation", mOrientation);
				
				mVertexTransformer.serialize(serializer, workData);
				mEye.serialize(serializer, workData);

				serializer->endMap(); // vertexTransformerObject
			}
		}

		bool VertexTransformerObject::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData)
		{
			uint32_t id;
			int32_t parentId;
			wp::Vector2 position, transformOffset;
			float orientation;
			VertexTransformer vertexTransformer;
			InfluenceEye eye;

			try
			{
				serializer->beginMap("vertexTransformerObject");
				{
					id = serializer->readUint32("id");
					parentId = (uint32_t)serializer->readInt32("parentId");

					// Update parent map to glue together later
					workData.vtoIdToVtoMap[id] = this;
					workData.vtoIdToParentMap[id] = parentId;

					position = serializer->readVector2("position");
					transformOffset = serializer->readVector2("transformOffset");
					orientation = serializer->readFloat("orientation");

					if (!vertexTransformer.deserialize(serializer, workData))
					{
						copyErrorsAndWarnings(&vertexTransformer, true, true);
						return false;
					}

					if (!eye.deserialize(serializer, workData))
					{
						copyErrorsAndWarnings(&eye, true, true);
						return false;
					}

					serializer->endMap(); // vertexTransformerObject
				}
			}
			catch (exception& e)
			{
				addDeserializationError(e.what());
				return false;
			}

			// Commit
			mId = id;
			mParent = nullptr; // For now
			mPosition = position;
			mTransformOffset = transformOffset;
			mOrientation = orientation;
			mVertexTransformer = vertexTransformer;
			mEye = eye;

			return true;
		}

		void VertexTransformerObject::setParent(VertexTransformerObject* parent)
		{
			mParent = parent;
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setPosition(wp::Vector2 const& position)
		{
			mPosition = position;
			invalidatePostTransform(true, true);
		}

		wp::Vector2 const& VertexTransformerObject::getPosition() const
		{
			return mPosition;
		}

		void VertexTransformerObject::setTransformOffset(wp::Vector2 const& offset)
		{
			mTransformOffset = offset;
			invalidatePostTransform(true, true);
		}

		wp::Vector2 const& VertexTransformerObject::getTransformOffset() const
		{
			return mTransformOffset;
		}

		void VertexTransformerObject::setOrientation(float orient)
		{
			mOrientation = orient;
		}

		float VertexTransformerObject::getOrientation() const
		{
			return mOrientation;
		}

		void VertexTransformerObject::setInfluenceEyeOriginOffset(wp::Vector2 const& originOffset)
		{
			mEye.setOriginOffset(originOffset);
			invalidatePostTransform(true, true);
		}

		wp::Vector2 const& VertexTransformerObject::getInfluenceEyeOriginOffset() const
		{
			return mEye.getOriginOffset();
		}

		wp::Vector2 VertexTransformerObject::getInfluenceEyeOriginPosition() const
		{
			return getPosition() + getInfluenceEyeOriginOffset();
		}

		void VertexTransformerObject::setInfluenceEyeAngleOffset(float offset)
		{
			mEye.setAngleOffset(offset - mOrientation);
			invalidatePostTransform(true, true);
		}

		float VertexTransformerObject::getInfluenceEyeAngleOffset() const
		{
			return mEye.getAngleOffset() + mOrientation;
		}

		VertexTransformer* VertexTransformerObject::getVertexTransformer()
		{
			return &mVertexTransformer;
		}

		wp::Vector2 VertexTransformerObject::calculateWorldPosition() const
		{
			auto pos = mPosition;

			if (mParent)
			{
				pos += mParent->calculateWorldPosition();
			}

			return pos;
		}

		wp::Vector2 VertexTransformerObject::transformVertex(wp::Vector2 const& v, bool* cacheChanged) const
		{
			return mVertexTransformer.transformVertex(v, calculateWorldPosition(), mTransformOffset, -mOrientation, mInputs, cacheChanged);
		}

		void VertexTransformerObject::setAnimationInterpolatorDefaultStructure(VertexTransformer::Key key, vector<Interpolator<float>::Point> const& points, vector<Interpolator<float>::Segment> const& segments, bool setToCurrent)
		{
			mVertexTransformer.setAnimationInterpolatorDefaultStructure(key, points, segments, setToCurrent);
			invalidatePostTransform(true, true);
		}

		Interpolator<float> const& VertexTransformerObject::getAnimationInterpolator(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getAnimationInterpolator(key);
		}

		Interpolator<float>& VertexTransformerObject::getAnimationInterpolator(VertexTransformer::Key key)
		{
			return mVertexTransformer.getAnimationInterpolator(key);
		}

		void VertexTransformerObject::setAnimationValues(VertexTransformer::Key key, vector<pair<float, float>> const& values)
		{
			mVertexTransformer.setAnimationValues(key, values);
			invalidatePostTransform(true, true);
		}

		vector<Interpolator<float>::Point> const& VertexTransformerObject::getAnimationValues(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getAnimationValues(key);
		}

		uint32_t VertexTransformerObject::getNumAnimationValues(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getNumAnimationValues(key);
		}

		void VertexTransformerObject::updateAnimationValue(VertexTransformer::Key key, uint32_t index, float time, float const& value)
		{
			mVertexTransformer.updateAnimationValue(key, index, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::addAnimationValue(VertexTransformer::Key key, float time, float value)
		{
			mVertexTransformer.addAnimationValue(key, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removeAnimationValue(VertexTransformer::Key key, uint32_t index)
		{
			mVertexTransformer.removeAnimationValue(key, index);
			invalidatePostTransform(true, true);
		}

		float VertexTransformerObject::getAnimationValue(VertexTransformer::Key key, float t) const
		{
			return mVertexTransformer.getAnimationValue(key, t);
		}

		void VertexTransformerObject::getAnimationScale(VertexTransformer::Key key, wp::Vector2* scaleMin, wp::Vector2* scaleMax)
		{
			mVertexTransformer.getAnimationScale(key, scaleMin, scaleMax);
		}

		void VertexTransformerObject::setAnimationEasing(VertexTransformer::Key key, uint32_t segment, Easing easing)
		{
			mVertexTransformer.setAnimationEasing(key, segment, easing);
			invalidatePostTransform(true, true);
		}

		vector<Interpolator<float>::Segment> const& VertexTransformerObject::getAnimationSegments(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getAnimationSegments(key);
		}

		vector<vector<Interpolator<float>::Point>> VertexTransformerObject::renderAnimation(VertexTransformer::Key key, float resolution) const
		{
			return mVertexTransformer.renderAnimation(key, resolution);
		}

		void VertexTransformerObject::addPointToAnimationInterpolator(VertexTransformer::Key key, float time, float value)
		{
			mVertexTransformer.addPointToAnimationInterpolator(key, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removePointFromAnimationInterpolator(VertexTransformer::Key key, uint32_t index)
		{
			mVertexTransformer.removePointFromAnimationInterpolator(key, index);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::updatePointInAnimationInterpolator(VertexTransformer::Key key, uint32_t index, float time, float value)
		{
			mVertexTransformer.updatePointInAnimationInterpolator(key, index, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setAnimationInterpolatorEasing(VertexTransformer::Key key, uint32_t index, Easing easing)
		{
			mVertexTransformer.setAnimationInterpolatorEasing(key, index, easing);
			invalidatePostTransform(true, true);
		}

		Interpolator<float> const& VertexTransformerObject::getInfluenceInterpolator(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getInfluenceInterpolator(key);
		}

		Interpolator<float>& VertexTransformerObject::getInfluenceInterpolator(VertexTransformer::Key key)
		{
			return mVertexTransformer.getInfluenceInterpolator(key);
		}

		void VertexTransformerObject::setInfluenceValues(VertexTransformer::Key key, vector<pair<float, float>> const& values)
		{
			mVertexTransformer.setInfluenceValues(key, values);
			invalidatePostTransform(true, true);
		}

		vector<Interpolator<float>::Point> const& VertexTransformerObject::getInfluenceValues(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getInfluenceValues(key);
		}

		uint32_t VertexTransformerObject::getNumInfluenceValues(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getNumInfluenceValues(key);
		}

		void VertexTransformerObject::updateInfluenceValue(VertexTransformer::Key key, uint32_t index, float time, float const& value)
		{
			mVertexTransformer.updateInfluenceValue(key, index, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::addInfluenceValue(VertexTransformer::Key key, float time, float value)
		{
			mVertexTransformer.addInfluenceValue(key, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removeInfluenceValue(VertexTransformer::Key key, uint32_t index)
		{
			mVertexTransformer.removeInfluenceValue(key, index);
			invalidatePostTransform(true, true);
		}

		float VertexTransformerObject::getInfluenceValue(VertexTransformer::Key key, float t) const
		{
			return mVertexTransformer.getInfluenceValue(key, t);
		}

		void VertexTransformerObject::getInfluenceScale(VertexTransformer::Key key, wp::Vector2* scaleMin, wp::Vector2* scaleMax)
		{
			mVertexTransformer.getInfluenceScale(key, scaleMin, scaleMax);
		}

		void VertexTransformerObject::setInfluenceEasing(VertexTransformer::Key key, uint32_t segment, Easing easing)
		{
			mVertexTransformer.setInfluenceEasing(key, segment, easing);
			invalidatePostTransform(true, true);
		}

		vector<Interpolator<float>::Segment> const& VertexTransformerObject::getInfluenceSegments(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getInfluenceSegments(key);
		}

		vector<vector<Interpolator<float>::Point>> VertexTransformerObject::renderInfluence(VertexTransformer::Key key, float resolution) const
		{
			return mVertexTransformer.renderInfluence(key, resolution);
		}

		void VertexTransformerObject::addPointToInfluenceInterpolator(VertexTransformer::Key key, float time, float value)
		{
			mVertexTransformer.addPointToInfluenceInterpolator(key, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removePointFromInfluenceInterpolator(VertexTransformer::Key key, uint32_t index)
		{
			mVertexTransformer.removePointFromInfluenceInterpolator(key, index);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::updatePointInInfluenceInterpolator(VertexTransformer::Key key, uint32_t index, float time, float value)
		{
			mVertexTransformer.updatePointInInfluenceInterpolator(key, index, time, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setInfluenceInterpolatorEasing(VertexTransformer::Key key, uint32_t index, Easing easing)
		{
			mVertexTransformer.setInfluenceInterpolatorEasing(key, index, easing);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setTransformOperand(VertexTransformer::Key key, uint32_t index, uint32_t operandIndex, tTransform::OperandType operand)
		{
			mVertexTransformer.setTransformOperand(key, index, operandIndex, operand);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setTransformInput(VertexTransformer::Key key, uint32_t index, uint32_t inputIndex, InputType input)
		{
			mVertexTransformer.setTransformInput(key, index, inputIndex, input);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setTransformConstant(VertexTransformer::Key key, uint32_t index, uint32_t constantIndex, float constant)
		{
			mVertexTransformer.setTransformConstant(key, index, constantIndex, constant);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setTransformFnMultiplier(VertexTransformer::Key key, uint32_t index, uint32_t fnMulIndex, float value)
		{
			mVertexTransformer.setTransformFnMultiplier(key, index, fnMulIndex, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setTransformTriggerLineIndex(VertexTransformer::Key key, uint32_t index, uint32_t indexIndex, uint32_t value)
		{
			mVertexTransformer.setTransformTriggerLineIndex(key, index, indexIndex, value);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setTransformOperation(VertexTransformer::Key key, uint32_t index, tTransform::Operation operation)
		{
			mVertexTransformer.setTransformOperation(key, index, operation);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::addScaleTransform(tTransform const& transform)
		{
			mVertexTransformer.addTransform(VertexTransformer::Key::Scale, transform);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removeScaleTransform(uint32_t index)
		{
			mVertexTransformer.removeTransform(VertexTransformer::Key::Scale, index);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::swapScaleTransforms(uint32_t index1, uint32_t index2)
		{
			mVertexTransformer.swapTransforms(VertexTransformer::Key::Scale, index1, index2);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setScaleTransforms(vector<tTransform> const& transforms)
		{
			mVertexTransformer.setScaleTransforms(transforms);
			invalidatePostTransform(true, true);
		}

		vector<tTransform> const& VertexTransformerObject::getScaleTransforms() const
		{
			return mVertexTransformer.getScaleTransforms();
		}

		void VertexTransformerObject::addAngleTransform(tTransform const& transform)
		{
			mVertexTransformer.addTransform(VertexTransformer::Key::Angle, transform);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removeAngleTransform(uint32_t index)
		{
			mVertexTransformer.removeTransform(VertexTransformer::Key::Angle, index);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::swapAngleTransforms(uint32_t index1, uint32_t index2)
		{
			mVertexTransformer.swapTransforms(VertexTransformer::Key::Angle, index1, index2);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setAngleTransforms(vector<tTransform> const& transforms)
		{
			mVertexTransformer.setAngleTransforms(transforms);
			invalidatePostTransform(true, true);
		}

		vector<tTransform> const& VertexTransformerObject::getAngleTransforms() const
		{
			return mVertexTransformer.getAngleTransforms();
		}

		void VertexTransformerObject::addOrbitAngleTransform(tTransform const& transform)
		{
			mVertexTransformer.addTransform(VertexTransformer::Key::OrbitAngle, transform);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removeOrbitAngleTransform(uint32_t index)
		{
			mVertexTransformer.removeTransform(VertexTransformer::Key::OrbitAngle, index);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::swapOrbitAngleTransforms(uint32_t index1, uint32_t index2)
		{
			mVertexTransformer.swapTransforms(VertexTransformer::Key::OrbitAngle, index1, index2);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setOrbitAngleTransforms(vector<tTransform> const& transforms)
		{
			mVertexTransformer.setOrbitAngleTransforms(transforms);
			invalidatePostTransform(true, true);
		}

		vector<tTransform> const& VertexTransformerObject::getOrbitAngleTransforms() const
		{
			return mVertexTransformer.getOrbitAngleTransforms();
		}

		void VertexTransformerObject::addOrbitDistanceTransform(tTransform const& transform)
		{
			mVertexTransformer.addTransform(VertexTransformer::Key::OrbitDistance, transform);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::removeOrbitDistanceTransform(uint32_t index)
		{
			mVertexTransformer.removeTransform(VertexTransformer::Key::OrbitDistance, index);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::swapOrbitDistanceTransforms(uint32_t index1, uint32_t index2)
		{
			mVertexTransformer.swapTransforms(VertexTransformer::Key::OrbitDistance, index1, index2);
			invalidatePostTransform(true, true);
		}

		void VertexTransformerObject::setOrbitDistanceTransforms(vector<tTransform> const& transforms)
		{
			mVertexTransformer.setOrbitDistanceTransforms(transforms);
			invalidatePostTransform(true, true);
		}

		vector<tTransform> const& VertexTransformerObject::getOrbitDistanceTransforms() const
		{
			return mVertexTransformer.getOrbitDistanceTransforms();
		}

		void VertexTransformerObject::updateTransformTriggerLineIndices(map<uint32_t, uint32_t> const& mapping)
		{
			mVertexTransformer.updateTransformTriggerLineIndices(mapping);
		}

		void VertexTransformerObject::setFollowOrbitAngle(bool follow)
		{
			mVertexTransformer.setFollowOrbitAngle(follow);
			invalidatePostTransform(true, true);
		}

		bool VertexTransformerObject::getFollowOrbitAngle() const
		{
			return mVertexTransformer.getFollowOrbitAngle();
		}

		void VertexTransformerObject::setCaptureMode(VertexTransformer::Key key, ValueCaptureMode mode)
		{
			mVertexTransformer.setCaptureMode(key, mode);
		}

		ValueCaptureMode VertexTransformerObject::getCaptureMode(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getCaptureMode(key);
		}

		float VertexTransformerObject::getCurCapturedValue(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getCurCapturedValue(key);
		}

		uint32_t VertexTransformerObject::getNumAnimatedPropertyEvents(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getNumAnimatedPropertyEvents(key);
		}

		vector<AnimatedPropertyEvent> const& VertexTransformerObject::getAnimatedPropertyEvents(VertexTransformer::Key key) const
		{
			return mVertexTransformer.getAnimatedPropertyEvents(key);
		}

		void VertexTransformerObject::addAnimatedPropertyEvent(VertexTransformer::Key key, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value)
		{
			mVertexTransformer.addAnimatedPropertyEvent(key, eventType, triggerType, value);
		}

		void VertexTransformerObject::removeAnimatedPropertyEvent(VertexTransformer::Key key, uint32_t index)
		{
			mVertexTransformer.removeAnimatedPropertyEvent(key, index);
		}

		void VertexTransformerObject::updateAnimatedPropertyEvent(VertexTransformer::Key key, uint32_t index, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value)
		{
			mVertexTransformer.updateAnimatedPropertyEvent(key, index, eventType, triggerType, value);
		}

		void VertexTransformerObject::setInputUserValue(uint32_t index, float value)
		{
			if (index >= 4)
			{
				throw CoreException("User input value index must be in [0, 3]");
			}

			mInputs.user[index] = value;
		}

		float VertexTransformerObject::getInputUserValue(uint32_t index) const
		{
			if (index >= 4)
			{
				throw CoreException("User input value index must be in [0, 3]");
			}

			return mInputs.user[index];
		}

		void VertexTransformerObject::setInputs(wp::Vector2 const& entityPosition, float entityAngle, vector<WorldTriggerLine*>* triggerLines)
		{
			if (isStatic())
			{
				return;
			}

			auto influenceOrigin = getInfluenceEyeOriginPosition();

			// Influence distance is just the distance to the eye scaled to [0, 1]
			auto infDist = entityPosition.distanceTo(influenceOrigin);

			// Influence angle is the angle to the eye, scaled to [0, 1], multipled by the distance to the
			// eye scaled to [0, 1].  Angle is adjusted so that a direction of [0, 1] is angle 0.  Angle is
			// anticlockwise.
			auto eyeDirToPos = influenceOrigin.directionTo(entityPosition);

			float infAngleRad = acosf(eyeDirToPos.y);
			auto infAngle = WP_RADTODEG(eyeDirToPos.x < 0 ? infAngleRad : WP_TAU - infAngleRad);
			infAngle += getInfluenceEyeAngleOffset();
			infAngle = clamp_angle(infAngle);

			// Set inputs
			mInputs.playerMove = mPrevEntityPosition != entityPosition;
			mInputs.playerTurn = mPrevEntityAngle != entityAngle;

			bool invalidate = mInputs.playerMove || mInputs.playerTurn;

			if (mInputs.entityInfluenceDistance != infDist)
			{
				mInputs.entityInfluenceDistance = infDist;
				invalidate = true;
			}

			if (mInputs.entityInfluenceAngle != infAngle)
			{
				mInputs.entityInfluenceAngle = infAngle;
				invalidate = true;
			}

			if (mInputs.entityGlobalAngle != entityAngle)
			{
				mInputs.entityGlobalAngle = entityAngle;
				invalidate = true;
			}

			mInputs.triggerLines = triggerLines;

			if (invalidate)
			{
				invalidatePostTransform(false, false);
			}

			mPrevEntityPosition = entityPosition;
			mPrevEntityAngle = entityAngle;
		}

		InputValue const& VertexTransformerObject::getInputs() const
		{
			return mInputs;
		}

		uint32_t VertexTransformerObject::calculateAnimationValues(double time)
		{
			uint32_t firedEvents{ 0 };

			for (uint32_t i = 0; i < (uint32_t)VertexTransformer::Key::COUNT; ++i)
			{
				mVertexTransformer.calculateAnimationValue((VertexTransformer::Key)i, mInputs, time, &firedEvents);
			}

			return firedEvents;
		}

		float VertexTransformerObject::transformT(VertexTransformer::Key key, double time) const
		{
			return mVertexTransformer.transformT(key, mInputs, time);
		}

	} // core
} // bw