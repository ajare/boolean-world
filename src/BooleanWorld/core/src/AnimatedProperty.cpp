#include "core/AnimatedProperty.h"
#include "core/CoreException.h"
#include "core/Interpolator.h"
#include "core/tTransform.h"
#include "core/InputValue.h"
#include "core/Serializable.h"
#include "core/Utils.h"

namespace bw {
namespace core {
using namespace std;

AnimatedProperty::AnimatedProperty()
    : AnimatedProperty("Unnamed") {
}

AnimatedProperty::AnimatedProperty(string const& name)
    : mName(name) {
  mAnimationInterpolator.setScale({0.0f, 0.0f}, {1.0f, 1.0f});
  mAnimationInterpolator.setDefaultStructure(
      {{0.0f, 0.5f}, {1.0f, 0.5f}},
      {{Easing::Linear}},
      true);

  mInfluenceInterpolator.setScale({0.0f, 0.0f}, {1.0f, 1.0f});
  mInfluenceInterpolator.setDefaultStructure(
      {{0.0f, 1.0f}, {1.0f, 1.0f}},
      {{Easing::Linear}},
      true);
}

AnimatedProperty::AnimatedProperty(string const& name, array<float, 2> const& animationRange, float animationDefault,
                                   std::array<float, 2> const& influenceRange, float influenceDefault)
    : mName(name) {
  mAnimationInterpolator.setScale({0.0f, animationRange[0]}, {1.0f, animationRange[1]});
  mAnimationInterpolator.setDefaultStructure(
      {{0.0f, animationDefault}, {1.0f, animationDefault}},
      {{Easing::Linear}},
      true);

  mInfluenceInterpolator.setScale({influenceRange[0], 0.0f}, {influenceRange[1], 1.0f});
  mInfluenceInterpolator.setDefaultStructure(
      {{influenceRange[0], influenceDefault}, {influenceRange[1], influenceDefault}},
      {{Easing::Linear}},
      true);
}

AnimatedProperty::AnimatedProperty(AnimatedProperty const& other) {
  copyFrom(other);
}

AnimatedProperty& AnimatedProperty::operator=(AnimatedProperty const& other) {
  copyFrom(other);
  return *this;
}

void AnimatedProperty::copyFrom(AnimatedProperty const& other) {
  mName = other.mName;
  mTransformFlow = other.mTransformFlow;
  mAnimationInterpolator = other.mAnimationInterpolator;
  mInfluenceInterpolator = other.mInfluenceInterpolator;
  mCapture = other.mCapture;
  mEvents = other.mEvents;
}

bool AnimatedProperty::childrenModified() const {
  return mTransformFlow.isModified() || mAnimationInterpolator.isModified() || mInfluenceInterpolator.isModified();
}

void AnimatedProperty::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("animatedProperty");
  {
    serializer->writeString("name", mName);

    mTransformFlow.serialize(serializer, workData);

    serializer->beginMap("animationInterpolator");
    {
      mAnimationInterpolator.serialize(serializer, workData);

      serializer->endMap();  // animationInterpolator
    }

    serializer->beginMap("influenceInterpolator");
    {
      mInfluenceInterpolator.serialize(serializer, workData);

      serializer->endMap();  // influenceInterpolator
    }

    serializer->writeUint32("captureMode", (uint32_t)mCapture.mode);

    serializer->beginArray("events");
    {
      for (auto const& event : mEvents) {
        serializer->beginMap("event");
        {
          serializer->writeUint32("eventType", event.eventType);
          serializer->writeUint32("triggerType", (uint32_t)event.triggerType);
          serializer->writeFloat("value", event.value);

          serializer->endMap();  // event
        }
      }

      serializer->endArray();  // events
    }

    serializer->endMap();  // animatedProperty
  }
}

bool AnimatedProperty::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  string name;
  TransformFlow transformFlow;
  Interpolator<float> animationInterpolator;
  Interpolator<float> influenceInterpolator;
  ValueCaptureMode captureMode;
  vector<AnimatedPropertyEvent> events;

  try {
    serializer->beginMap("animatedProperty");
    {
      name = serializer->readString("name");

      if (!transformFlow.deserialize(serializer, workData)) {
        copyErrorsAndWarnings(&transformFlow, true, true);
        return false;
      }

      serializer->beginMap("animationInterpolator");
      {
        if (!animationInterpolator.deserialize(serializer, workData)) {
          copyErrorsAndWarnings(&animationInterpolator, true, true);
          return false;
        }

        serializer->endMap();  // animationInterpolator
      }

      serializer->beginMap("influenceInterpolator");
      {
        if (!influenceInterpolator.deserialize(serializer, workData)) {
          copyErrorsAndWarnings(&influenceInterpolator, true, true);
          return false;
        }

        serializer->endMap();  // influenceInterpolator
      }

      captureMode = (ValueCaptureMode)serializer->readUint32("captureMode");

      serializer->beginArray("events");
      {
        while (serializer->nextArrayItem()) {
          serializer->beginMap("event");
          {
            auto eventType = serializer->readUint32("eventType");
            auto triggerType = (AnimatedPropertyEventTriggerType)serializer->readUint32("triggerType");
            auto value = serializer->readFloat("value");

            events.push_back({eventType, triggerType, value});
            serializer->endMap();  // event
          }
        }

        serializer->endArray();  // events
      }

      serializer->endMap();  // animatedProperty
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mName = name;
  mTransformFlow = transformFlow;
  mAnimationInterpolator = animationInterpolator;
  mInfluenceInterpolator = influenceInterpolator;
  mCapture = captureMode;
  mEvents = events;

  return true;
}

bool AnimatedProperty::isStatic() const {
  return mAnimationInterpolator.isStatic();
}

void AnimatedProperty::resetCapture() {
  mCapture.prevValue = numeric_limits<float>::quiet_NaN();
  mCapture.curValue = 0.0f;
}

void AnimatedProperty::reset() {
  mTransformFlow = TransformFlow();
  mAnimationInterpolator.reset();
  mInfluenceInterpolator.reset();
  mCapture = ValueCaptureMode::DistanceSticky;
}

float AnimatedProperty::processValueCapture(float curValue, float newValue) const {
  float deltaDistValue, newDistValue, checkDistValue;
  float* prevDistValue = &mCapture.prevValue;
  float* curDistValue = &mCapture.curValue;

  if (isnan(*prevDistValue)) {
    *curDistValue = curValue;
    *prevDistValue = curValue;
    deltaDistValue = 0.0f;
  } else {
    deltaDistValue = newValue - *prevDistValue;

    switch (mCapture.mode) {
      case ValueCaptureMode::AngleSticky:
      case ValueCaptureMode::AngleDeltaUp:
      case ValueCaptureMode::AngleDeltaDown:
      case ValueCaptureMode::AngleLatchedUp:
      case ValueCaptureMode::AngleLatchedDown:
        if (deltaDistValue >= 0.5) {
          deltaDistValue -= 1.0f;
        } else if ((*prevDistValue - newValue) >= 0.5f) {
          deltaDistValue += 1.0f;
        }
        break;

      default:
        break;
    }
  }

  switch (mCapture.mode) {
    case ValueCaptureMode::DistanceSticky:
    case ValueCaptureMode::AngleSticky:
      *curDistValue = newValue;
      break;

    case ValueCaptureMode::DistanceDeltaUp:
    case ValueCaptureMode::AngleDeltaUp:
      if (deltaDistValue > 0.0f) {
        *curDistValue += deltaDistValue;
      }
      break;

    case ValueCaptureMode::DistanceDeltaDown:
    case ValueCaptureMode::AngleDeltaDown:
      if (deltaDistValue < 0.0f) {
        *curDistValue += deltaDistValue;
      }
      break;

    case ValueCaptureMode::DistanceLatchedUp:
      newDistValue = *prevDistValue + deltaDistValue;

      if (newDistValue > *curDistValue) {
        *curDistValue += deltaDistValue;
      }
      break;

    case ValueCaptureMode::DistanceLatchedDown:
      newDistValue = *prevDistValue + deltaDistValue;

      if (newDistValue < *curDistValue) {
        *curDistValue += deltaDistValue;
      }
      break;

    case ValueCaptureMode::AngleLatchedUp:
      newDistValue = *prevDistValue + deltaDistValue;
      checkDistValue = *curDistValue + ((newDistValue >= 1.0f) ? 1.0f : 0.0f);

      if ((*prevDistValue <= *curDistValue) && newDistValue > checkDistValue) {
        *curDistValue += deltaDistValue;
      }
      break;

    case ValueCaptureMode::AngleLatchedDown:
      newDistValue = *prevDistValue + deltaDistValue;
      checkDistValue = *curDistValue - ((newDistValue < 0.0f) ? 1.0f : 0.0f);

      if ((*prevDistValue >= *curDistValue) && (newDistValue < checkDistValue)) {
        *curDistValue += deltaDistValue;
      }
      break;

    default:
      throw CoreException("Unknown/unsupported ValueCaptureMode");
  }

  // Update previous values
  *prevDistValue = newValue;

  switch (mCapture.mode) {
    case ValueCaptureMode::DistanceSticky:
    case ValueCaptureMode::DistanceDeltaUp:
    case ValueCaptureMode::DistanceDeltaDown:
    case ValueCaptureMode::DistanceLatchedUp:
    case ValueCaptureMode::DistanceLatchedDown:
      *curDistValue = clamp_unit(*curDistValue);
      break;

    case ValueCaptureMode::AngleSticky:
    case ValueCaptureMode::AngleDeltaUp:
    case ValueCaptureMode::AngleDeltaDown:
    case ValueCaptureMode::AngleLatchedUp:
    case ValueCaptureMode::AngleLatchedDown:
      while (*curDistValue < 0.0f) {
        *curDistValue += 1.0f;
      }
      while (*curDistValue >= 1.0f) {
        *curDistValue -= 1.0f;
      }
      break;

    default:
      break;
  }

  return *curDistValue;
}

float AnimatedProperty::transformT(InputValue const& inputs, double time) const {
  return mTransformFlow.transformT(inputs, time);
}

float AnimatedProperty::captureValue(float value) const {
  return processValueCapture(mCapture.curValue, value);
}

void AnimatedProperty::setTransforms(vector<tTransform> const& transforms) {
  mTransformFlow.setTransforms(transforms);
}

vector<tTransform>& AnimatedProperty::getTransforms() {
  return mTransformFlow.getTransforms();
}

vector<tTransform> const& AnimatedProperty::getTransforms() const {
  return mTransformFlow.getTransforms();
}

Interpolator<float>& AnimatedProperty::getAnimationInterpolator() {
  return mAnimationInterpolator;
}

Interpolator<float> const& AnimatedProperty::getAnimationInterpolator() const {
  return mAnimationInterpolator;
}

Interpolator<float>& AnimatedProperty::getInfluenceInterpolator() {
  return mInfluenceInterpolator;
}

Interpolator<float> const& AnimatedProperty::getInfluenceInterpolator() const {
  return mInfluenceInterpolator;
}

float AnimatedProperty::getCurCapturedValue() const {
  return mCapture.curValue;
}

void AnimatedProperty::setCaptureMode(ValueCaptureMode mode) {
  mCapture.mode = mode;
}

ValueCaptureMode AnimatedProperty::getCaptureMode() const {
  return mCapture.mode;
}

uint32_t AnimatedProperty::getNumEvents() const {
  return (uint32_t)mEvents.size();
}

vector<AnimatedPropertyEvent> const& AnimatedProperty::getEvents() const {
  return mEvents;
}

void AnimatedProperty::addEvent(uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value) {
  mEvents.push_back({eventType, triggerType, value});
}

void AnimatedProperty::removeEvent(uint32_t index) {
  if (index >= mEvents.size()) {
    throw CoreException("index out of range");
  }
  mEvents.erase(mEvents.begin() + index);
}

void AnimatedProperty::updateEvent(uint32_t index, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value) {
  auto numEvents = (int)getNumEvents();
  assert((int)index < numEvents && "AnimatedProperty::removeEvent(index) - index out of bounds");

  mEvents[index] = {eventType, triggerType, value};
}

}  // namespace core
}  // namespace bw
