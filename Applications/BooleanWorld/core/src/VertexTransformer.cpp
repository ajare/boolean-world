#include "core/VertexTransformer.h"
#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/Utils.h"


namespace bw
{
	namespace core
	{
		using namespace std;

		VertexTransformer::VertexTransformer()
			: mFollowOrbitAngle(false)
			, mCacheStaticness(false)
			, mIsStatic(false)
			, mAnimators{string("Scale"), string("Angle"), string("OrbitAngle"), string("OrbitDistance")}
		{
			mAnimators[(int)Key::Scale].initialiseAnimation({ 1.0f, BW_INTERPOLATOR_MAX_SCALE }, 1.0f);
			mAnimators[(int)Key::Angle].initialiseAnimation({ 0.0f, BW_INTERPOLATOR_MAX_ANGLE }, 0.0f);
			mAnimators[(int)Key::OrbitAngle].initialiseAnimation({ 0.0f, BW_INTERPOLATOR_MAX_ANGLE }, 0.0f);
			mAnimators[(int)Key::OrbitDistance].initialiseAnimation({ 0.0f, BW_INTERPOLATOR_MAX_DISTANCE }, 100.0f);

			mAnimators[(int)Key::Scale].initialiseInfluence({ 0.0f, BW_INTERPOLATOR_MAX_DISTANCE }, 1.0f);
			mAnimators[(int)Key::Angle].initialiseInfluence({ 0.0f, BW_INTERPOLATOR_MAX_DISTANCE }, 1.0f);
			mAnimators[(int)Key::OrbitAngle].initialiseInfluence({ 0.0f, BW_INTERPOLATOR_MAX_DISTANCE }, 1.0f);
			mAnimators[(int)Key::OrbitDistance].initialiseInfluence({ 0.0f, BW_INTERPOLATOR_MAX_DISTANCE }, 1.0f);

			for (int i = 0; i < (int)Key::COUNT; ++i)
			{
				if (i == (int)Key::Scale)
				{
					mCurValues[i] = 0.5f;
				}
				else
				{
					mCurValues[i] = 0.0f;
				}

				mPrevValues[i] = -999991.0f;
			}
		}

		VertexTransformer::VertexTransformer(VertexTransformer const& other)
		{
			copyFrom(other);
		}

		VertexTransformer& VertexTransformer::operator=(VertexTransformer const& other)
		{
			copyFrom(other);
			return *this;
		}

		bool VertexTransformer::childrenModified() const
		{
			for (int i = 0; i < (int)Key::COUNT; ++i)
			{
				if (mAnimators[i].isModified())
				{
					return true;
				}
			}

			return false;
		}

		void VertexTransformer::copyFrom(VertexTransformer const& other)
		{
			for (int i = 0; i < (int)Key::COUNT; ++i)
			{
				mAnimators[i] = other.mAnimators[i];
				mCurValues[i] = other.mCurValues[i];
				mPrevValues[i] = other.mPrevValues[i];
			}

			mFollowOrbitAngle = other.mFollowOrbitAngle;
			mCacheStaticness = other.mCacheStaticness;
			mIsStatic = other.mIsStatic;
		}

		void VertexTransformer::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const
		{
			serializer->beginMap("vertexTransformer");
			{
				serializer->writeBool("followOrbitAngle", mFollowOrbitAngle);

				string interpolators[(int)Key::COUNT] = {
					"scale", "angle", "orbitAngle", "orbitDistance"
				};

				serializer->beginArray("animators");
				{
					for (int i = 0; i < (int)Key::COUNT; ++i)
					{
						serializer->beginMap(interpolators[i]);
						{
							mAnimators[i].serialize(serializer, workData);
							serializer->endMap();
						}
					}

					serializer->endArray(); // animators
				}

				serializer->endMap(); // vertexTransformer
			}
		}

		bool VertexTransformer::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData)
		{
			AnimatedProperty animators[(int)Key::COUNT];
			bool followOrbitAngle;

			try
			{
				serializer->beginMap("vertexTransformer");
				{
					followOrbitAngle = serializer->readBool("followOrbitAngle");

					string interpolators[(int)Key::COUNT] = {
						"scale", "angle", "orbitAngle", "orbitDistance"
					};

					serializer->beginArray("animators");
					{
						int i = 0;

						while (serializer->nextArrayItem())
						{
							serializer->beginMap(interpolators[i]);
							{
								if (!animators[i].deserialize(serializer, workData))
								{
									copyErrorsAndWarnings(&animators[i], true, true);
									return false;
								}

								i++;
								serializer->endMap();
							}
						}

						serializer->endArray(); // animators
					}

					serializer->endMap(); // vertexTransformer
				}
			}
			catch (exception& e)
			{
				addDeserializationError(e.what());
				return false;
			}

			// Commit
			mFollowOrbitAngle = followOrbitAngle;

			for (int i = 0; i < (int)Key::COUNT; ++i)
			{
				mAnimators[i] = animators[i];
			}

			return true;
		}

		bool VertexTransformer::isStatic() const
		{
			if (mCacheStaticness)
			{
				return mIsStatic;
			}

			for (int i = 0; i < (int)Key::COUNT; ++i)
			{
				if (!mAnimators[i].isStatic())
				{
					return false;
				}
			}

			return true;
		}

		void VertexTransformer::cacheStaticness(bool cache)
		{
			if (cache)
			{
				mCacheStaticness = false;
				mIsStatic = isStatic();
			}

			mCacheStaticness = cache;
		}

		void VertexTransformer::resetAnimatorCaptures()
		{
			for (int i = 0; i < (int)Key::COUNT; ++i)
			{
				mAnimators[i].resetCapture();
			}
		}

		void VertexTransformer::resetAnimator(Key key)
		{
			mAnimators[(int)key].reset();
		}

		void VertexTransformer::removeTransform(vector<tTransform>& flow, uint32_t index)
		{
			auto numTransforms = (uint32_t)flow.size();

			for (uint32_t i = index; i < numTransforms - 1; ++i)
			{
				flow[i] = flow[i + 1];
			}

			flow.pop_back();
		}

		void VertexTransformer::setTransformOperand(Key key, uint32_t index, uint32_t operandIndex, tTransform::OperandType operand)
		{
			auto& tff = mAnimators[(int)key].getTransforms();
			auto& tf = tff[index];

			tf.operands[operandIndex] = operand;
		}

		void VertexTransformer::setTransformInput(Key key, uint32_t index, uint32_t inputIndex, InputType input)
		{
			auto& tff = mAnimators[(int)key].getTransforms();
			auto& tf = tff[index];

			tf.inputs[inputIndex] = input;
		}

		void VertexTransformer::setTransformConstant(Key key, uint32_t index, uint32_t constantIndex, float constant)
		{
			auto& tff = mAnimators[(int)key].getTransforms();
			auto& tf = tff[index];

			tf.constants[constantIndex] = constant;
		}

		void VertexTransformer::setTransformFnMultiplier(Key key, uint32_t index, uint32_t fnMulIndex, float value)
		{
			auto& tff = mAnimators[(int)key].getTransforms();
			auto& tf = tff[index];

			tf.fnMultipliers[fnMulIndex] = value;
		}

		void VertexTransformer::setTransformTriggerLineIndex(Key key, uint32_t index, uint32_t indexIndex, uint32_t value)
		{
			auto& tff = mAnimators[(int)key].getTransforms();
			auto& tf = tff[index];

			tf.indices[indexIndex] = value;
		}

		void VertexTransformer::setTransformOperation(Key key, uint32_t index, tTransform::Operation operation)
		{
			auto& tff = mAnimators[(int)key].getTransforms();
			auto& tf = tff[index];

			tf.operation = operation;
		}

		void VertexTransformer::addTransform(Key key, tTransform const& transform)
		{
			mAnimators[(int)key].getTransforms().push_back(transform);
		}

		void VertexTransformer::removeTransform(Key key, uint32_t index)
		{
			removeTransform(mAnimators[(int)key].getTransforms(), index);
		}

		void VertexTransformer::swapTransforms(Key key, uint32_t index1, uint32_t index2)
		{
			auto& transforms = mAnimators[(int)key].getTransforms();
			swap(transforms[index1], transforms[index2]);
		}

		void VertexTransformer::setScaleTransforms(vector<tTransform> const& transforms)
		{
			mAnimators[(int)Key::Scale].setTransforms(transforms);
		}

		vector<tTransform> const& VertexTransformer::getScaleTransforms() const
		{
			return mAnimators[(int)Key::Scale].getTransforms();
		}

		void VertexTransformer::setAngleTransforms(vector<tTransform> const& transforms)
		{
			mAnimators[(int)Key::Angle].setTransforms(transforms);
		}

		vector<tTransform> const& VertexTransformer::getAngleTransforms() const
		{
			return mAnimators[(int)Key::Angle].getTransforms();
		}

		void VertexTransformer::setOrbitAngleTransforms(vector<tTransform> const& transforms)
		{
			mAnimators[(int)Key::OrbitAngle].setTransforms(transforms);
		}

		vector<tTransform> const& VertexTransformer::getOrbitAngleTransforms() const
		{
			return mAnimators[(int)Key::OrbitAngle].getTransforms();
		}

		void VertexTransformer::setOrbitDistanceTransforms(vector<tTransform> const& transforms)
		{
			mAnimators[(int)Key::OrbitDistance].setTransforms(transforms);
		}

		vector<tTransform> const& VertexTransformer::getOrbitDistanceTransforms() const
		{
			return mAnimators[(int)Key::OrbitDistance].getTransforms();
		}

		void VertexTransformer::updateTransformTriggerLineIndices(map<uint32_t, uint32_t> const& mapping)
		{
			for (int i = 0; i < (int)Key::COUNT; ++i)
			{
				auto& transforms = mAnimators[i].getTransforms();

				for (auto& transform : transforms)
				{
					for (int j = 0; j < 2; ++j)
					{
						if (transform.operands[j] == bw::core::tTransform::OperandType::TriggerLine ||
							transform.operands[j] == bw::core::tTransform::OperandType::TriggerLineRed ||
							transform.operands[j] == bw::core::tTransform::OperandType::TriggerLineBlue)
						{
							transform.indices[j] = mapping.at(transform.indices[j]);
						}
					}
				}
			}
		}

		void VertexTransformer::setAnimationInterpolatorDefaultStructure(VertexTransformer::Key key, vector<Interpolator<float>::Point> const& points, vector<Interpolator<float>::Segment> const& segments, bool setToCurrent)
		{
			mAnimators[(int)key].setAnimationInterpolatorDefaultStructure(points, segments, setToCurrent);
		}

		void VertexTransformer::setAnimationValues(Key key, vector<std::pair<float, float>> const& values)
		{
			mAnimators[(int)key].setAnimationValues(values);
		}

		vector<Interpolator<float>::Point> const& VertexTransformer::getAnimationValues(Key key) const
		{
			return mAnimators[(int)key].getAnimationValues();
		}

		uint32_t VertexTransformer::getNumAnimationValues(Key key) const
		{
			return mAnimators[(int)key].getNumAnimationValues();
		}

		void VertexTransformer::updateAnimationValue(Key key, uint32_t index, float time, float const& value)
		{
			mAnimators[(int)key].updateAnimationValue(index, time, value);
		}

		void VertexTransformer::addAnimationValue(Key key, float time, float value)
		{
			mAnimators[(int)key].addAnimationValue(time, value);
		}

		void VertexTransformer::removeAnimationValue(Key key, uint32_t index)
		{
			mAnimators[(int)key].removeAnimationValue(index);
		}

		float VertexTransformer::getAnimationValue(Key key, float time) const
		{
			return mAnimators[(int)key].getAnimationValue(time);
		}

		void VertexTransformer::getAnimationScale(Key key, wp::Vector2* scaleMin, wp::Vector2* scaleMax)
		{
			mAnimators[(int)key].getAnimationScale(scaleMin, scaleMax);
		}

		void VertexTransformer::setAnimationEasing(Key key, uint32_t segment, Easing easing)
		{
			mAnimators[(int)key].setAnimationEasing(segment, easing);
		}

		vector<Interpolator<float>::Segment> const& VertexTransformer::getAnimationSegments(Key key) const
		{
			return mAnimators[(int)key].getAnimationSegments();
		}

		vector<vector<Interpolator<float>::Point>> VertexTransformer::renderAnimation(Key key, float resolution) const
		{
			return mAnimators[(int)key].renderAnimation(resolution);
		}

		void VertexTransformer::addPointToAnimationInterpolator(Key key, float time, float value)
		{
			mAnimators[(int)key].addAnimationValue(time, value);
		}

		void VertexTransformer::removePointFromAnimationInterpolator(Key key, uint32_t index)
		{
			mAnimators[(int)key].removeAnimationValue(index);
		}

		void VertexTransformer::updatePointInAnimationInterpolator(Key key, uint32_t index, float time, float value)
		{
			mAnimators[(int)key].updateAnimationValue(index, time, value);
		}

		void VertexTransformer::setAnimationInterpolatorEasing(Key key, uint32_t index, Easing easing)
		{
			mAnimators[(int)key].setAnimationEasing(index, easing);
		}

		Interpolator<float> const& VertexTransformer::getAnimationInterpolator(Key key) const
		{
			return mAnimators[(int)key].getAnimationInterpolator();
		}

		Interpolator<float>& VertexTransformer::getAnimationInterpolator(Key key)
		{
			return mAnimators[(int)key].getAnimationInterpolator();
		}

		void VertexTransformer::setInfluenceValues(Key key, vector<pair<float, float>> const& values)
		{
			mAnimators[(int)key].setInfluenceValues(values);
		}

		vector<Interpolator<float>::Point> const& VertexTransformer::getInfluenceValues(Key key) const
		{
			return mAnimators[(int)key].getInfluenceValues();
		}

		uint32_t VertexTransformer::getNumInfluenceValues(Key key) const
		{
			return mAnimators[(int)key].getNumInfluenceValues();
		}

		void VertexTransformer::updateInfluenceValue(Key key, uint32_t index, float time, float const& value)
		{
			mAnimators[(int)key].updateInfluenceValue(index, time, value);
		}

		void VertexTransformer::addInfluenceValue(Key key, float time, float value)
		{
			mAnimators[(int)key].addInfluenceValue(time, value);
		}

		void VertexTransformer::removeInfluenceValue(Key key, uint32_t index)
		{
			mAnimators[(int)key].removeInfluenceValue(index);
		}

		float VertexTransformer::getInfluenceValue(Key key, float time) const
		{
			return mAnimators[(int)key].getInfluenceValue(time);
		}

		void VertexTransformer::getInfluenceScale(Key key, wp::Vector2* scaleMin, wp::Vector2* scaleMax)
		{
			mAnimators[(int)key].getInfluenceScale(scaleMin, scaleMax);
		}

		void VertexTransformer::setInfluenceEasing(Key key, uint32_t segment, Easing easing)
		{
			mAnimators[(int)key].setInfluenceEasing(segment, easing);
		}

		vector<Interpolator<float>::Segment> const& VertexTransformer::getInfluenceSegments(Key key) const
		{
			return mAnimators[(int)key].getInfluenceSegments();
		}

		vector<vector<Interpolator<float>::Point>> VertexTransformer::renderInfluence(Key key, float resolution) const
		{
			return mAnimators[(int)key].renderInfluence(resolution);
		}

		void VertexTransformer::addPointToInfluenceInterpolator(Key key, float time, float value)
		{
			mAnimators[(int)key].addInfluenceValue(time, value);
		}

		void VertexTransformer::removePointFromInfluenceInterpolator(Key key, uint32_t index)
		{
			mAnimators[(int)key].removeInfluenceValue(index);
		}

		void VertexTransformer::updatePointInInfluenceInterpolator(Key key, uint32_t index, float time, float value)
		{
			mAnimators[(int)key].updateInfluenceValue(index, time, value);
		}

		void VertexTransformer::setInfluenceInterpolatorEasing(Key key, uint32_t index, Easing easing)
		{
			mAnimators[(int)key].setInfluenceEasing(index, easing);
		}

		Interpolator<float>& VertexTransformer::getInfluenceInterpolator(Key key)
		{
			return mAnimators[(int)key].getInfluenceInterpolator();
		}

		Interpolator<float> const& VertexTransformer::getInfluenceInterpolator(Key key) const
		{
			return mAnimators[(int)key].getInfluenceInterpolator();
		}

		void VertexTransformer::setFollowOrbitAngle(bool follow)
		{
			mFollowOrbitAngle = follow;
		}

		bool VertexTransformer::getFollowOrbitAngle() const
		{
			return mFollowOrbitAngle;
		}

		void VertexTransformer::setCaptureMode(Key key, ValueCaptureMode mode)
		{
			mAnimators[(int)key].setCaptureMode(mode);
		}

		ValueCaptureMode VertexTransformer::getCaptureMode(Key key) const
		{
			return mAnimators[(int)key].getCaptureMode();
		}

		float VertexTransformer::getCurCapturedValue(Key key) const
		{
			return mAnimators[(int)key].getCurCapturedValue();
		}

		float VertexTransformer::calculateAnimationValue(VertexTransformer::Key key, InputValue const& inputs, double globalTime, uint32_t* firedEvents)
		{
			auto influenceAmt = BW_INTERPOLATOR_MAX_DISTANCE - inputs.entityInfluenceDistance;

			float cap = mAnimators[(int)key].captureValue(transformT(key, inputs, globalTime));
			float value = getAnimationValue(key, cap);
			float infl = getInfluenceValue(key, influenceAmt);

			switch (key)
			{
			case Key::Angle:
			case Key::OrbitAngle:
			case Key::OrbitDistance:
				value *= infl;
				break;

			case Key::Scale:
				value = 0.5f + (value - 1.0f) * 0.5f * infl;
				break;

			default:
				throw exception("Bad VertexTransformer::Key");
			}

			// Update values
			mPrevValues[(int)key] = mCurValues[(int)key];
			mCurValues[(int)key] = value;
			
			// Check events
			auto const& events = mAnimators[(int)key].getEvents();

			for (auto const& event : events)
			{
				if (checkAnimatedPropertyEvent(event, mPrevValues[(int)key], value))
				{
					*firedEvents |= event.eventType;
				}
			}

			return value;
		}

		bool VertexTransformer::checkAnimatedPropertyEvent(AnimatedPropertyEvent const& event, float oldValue, float newValue) const
		{
			if (oldValue < -999990.0f)
			{
				return false;
			}

			bool crossedUp = oldValue < event.value && newValue >= event.value;
			bool crossedDown = oldValue > event.value && newValue <= event.value;

			switch (event.triggerType)
			{
			case AnimatedPropertyEventTriggerType::UpDown:
				return crossedUp || crossedDown;

			case AnimatedPropertyEventTriggerType::Up:
				return crossedUp;

			case AnimatedPropertyEventTriggerType::Down:
				return crossedDown;
			
			default:
				throw exception("Unhandled AnimatedPropertyEventTriggerType");
			}
		}

		float VertexTransformer::captureAndCacheAnimationValue(VertexTransformer::Key key, bool* cacheChanged) const
		{
			float value = mCurValues[(int)key];

			if (cacheChanged && value != mPrevValues[(int)key])
			{
				*cacheChanged = true;
			}

			return value;
		}

		uint32_t VertexTransformer::getNumAnimatedPropertyEvents(Key key) const
		{
			return mAnimators[(int)key].getNumEvents();
		}

		vector<AnimatedPropertyEvent> const& VertexTransformer::getAnimatedPropertyEvents(Key key) const
		{
			return mAnimators[(int)key].getEvents();
		}

		void VertexTransformer::addAnimatedPropertyEvent(Key key, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value)
		{
			mAnimators[(int)key].addEvent(eventType, triggerType, value);
		}

		void VertexTransformer::removeAnimatedPropertyEvent(Key key, uint32_t index)
		{
			mAnimators[(int)key].removeEvent(index);
		}

		void VertexTransformer::updateAnimatedPropertyEvent(Key key, uint32_t index, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value)
		{
			mAnimators[(int)key].updateEvent(index, eventType, triggerType, value);
		}

		wp::Vector2 VertexTransformer::transformVertex(wp::Vector2 const& v, wp::Vector2 const& objectPosition, wp::Vector2 const& transformOffset, float orientation, InputValue const& inputs, bool* cacheChanged) const
		{
			if (cacheChanged)
			{
				*cacheChanged = false;
			}

			auto vt = v;
			auto influenceAmt = BW_INTERPOLATOR_MAX_DISTANCE - inputs.entityInfluenceDistance;
			
			auto orbitAngle = captureAndCacheAnimationValue(Key::OrbitAngle, cacheChanged) + orientation;
			auto localAngle = captureAndCacheAnimationValue(Key::Angle, cacheChanged) + (getFollowOrbitAngle() ? orbitAngle : 0);
			auto scale = captureAndCacheAnimationValue(Key::Scale, cacheChanged);
			auto transformOrigin = transformOffset / scale;

			// First, orient around the origin
			vt.rotateAnticlockwise(orientation);

			// Offset the vertex by the transform origin
			vt -= transformOrigin;
			
			// Next rotate and scale the vertex around its local transform origin.
			vt.rotateAnticlockwise(localAngle);
			vt *= scale;

			// Orbit
			auto orbitDir = wp::Vector2::UNIT_Y.rotatedAnticlockwiseCopy(orbitAngle);
			auto orbitDist = captureAndCacheAnimationValue(Key::OrbitDistance, cacheChanged);

			vt += orbitDir * orbitDist;

			// Remove offset
			vt += transformOffset;

			// Translate into world space
			vt += objectPosition;

			return vt;
		}

		float VertexTransformer::transformT(Key key, InputValue const& inputs, double time) const
		{
			return mAnimators[(int)key].transformT(inputs, time);
		}

	} // core
} // bw