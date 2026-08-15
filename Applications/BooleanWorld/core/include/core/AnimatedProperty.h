#pragma once

#include <array>
#include <vector>

#include "core/Platform.h"
#include "core/Interpolator.h"
#include "core/TransformFlow.h"
#include "core/InputValue.h"
#include "core/ValueCaptureMode.h"
#include "core/ValueCapture.h"
#include "core/AnimatedPropertyEvent.h"
#include "core/Serializable.h"


namespace bw
{
	namespace core
	{

		class AnimatedProperty : public Serializable
		{
			std::string mName;

			TransformFlow mTransformFlow;

			Interpolator<float> mAnimationInterpolator;

			Interpolator<float> mInfluenceInterpolator;

			mutable ValueCapture mCapture;

			std::vector<AnimatedPropertyEvent> mEvents;

		private:

			bool childrenModified() const override;

			float processValueCapture(float curValue, float newValue) const;

		protected:

			void copyFrom(AnimatedProperty const& other);

			void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

			bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

		public:

			AnimatedProperty();

			AnimatedProperty(std::string const& name);

			AnimatedProperty(std::string const& name, std::array<float, 2> const& animationRange, float animationDefault,
				std::array<float, 2> const& influenceRange, float influenceDefault);

			AnimatedProperty(AnimatedProperty const& other);

			AnimatedProperty& operator=(AnimatedProperty const& other);

			bool isStatic() const;

			void resetCapture();

			void reset();

			float transformT(InputValue const& inputs, double time) const;

			float captureValue(float value) const;

			// Transform flow
			void setTransforms(std::vector<tTransform> const& transforms);

			std::vector<tTransform>& getTransforms();

			std::vector<tTransform> const& getTransforms() const;

			// Animation interpolator
			void setAnimationInterpolatorDefaultStructure(std::vector<Interpolator<float>::Point> const& points, std::vector<Interpolator<float>::Segment> const& segments, bool setToCurrent);

			void initialiseAnimation(std::array<float, 2> const& animationRange, float animationDefault);

			void setAnimationValues(std::vector<std::pair<float, float>> const& values);

			std::vector<Interpolator<float>::Point> const& getAnimationValues() const;

			uint32_t getNumAnimationValues() const;

			void updateAnimationValue(uint32_t index, float time, float const& value);

			void addAnimationValue(float time, float value);

			void removeAnimationValue(uint32_t index);

			float getAnimationValue(float time) const;

			void getAnimationScale(wp::Vector2* scaleMin, wp::Vector2* scaleMax);

			void setAnimationEasing(uint32_t segment, Easing easing);

			std::vector<Interpolator<float>::Segment> const& getAnimationSegments() const;

			std::vector<std::vector<Interpolator<float>::Point>> renderAnimation(float resolution) const;

			Interpolator<float>& getAnimationInterpolator();

			Interpolator<float> const& getAnimationInterpolator() const;

			float getCurCapturedValue() const;

			// Influence interpolator
			void initialiseInfluence(std::array<float, 2> const& influenceRange, float influenceDefault);

			void setInfluenceValues(std::vector<std::pair<float, float>> const& values);

			std::vector<Interpolator<float>::Point> const& getInfluenceValues() const;

			uint32_t getNumInfluenceValues() const;

			void updateInfluenceValue(uint32_t index, float time, float const& value);

			void addInfluenceValue(float time, float value);

			void removeInfluenceValue(uint32_t index);

			float getInfluenceValue(float time) const;

			void getInfluenceScale(wp::Vector2* scaleMin, wp::Vector2* scaleMax);

			void setInfluenceEasing(uint32_t segment, Easing easing);

			std::vector<Interpolator<float>::Segment> const& getInfluenceSegments() const;

			std::vector<std::vector<Interpolator<float>::Point>> renderInfluence(float resolution) const;

			Interpolator<float>& getInfluenceInterpolator();

			Interpolator<float> const& getInfluenceInterpolator() const;

			// Capture
			void setCaptureMode(ValueCaptureMode mode);

			ValueCaptureMode getCaptureMode() const;

			// Events
			uint32_t getNumEvents() const;

			std::vector<AnimatedPropertyEvent> const& getEvents() const;

			void addEvent(uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value);

			void removeEvent(uint32_t index);

			void updateEvent(uint32_t index, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value);
		};

	} // core
} // bw
