#include "core/VertexTransformer.h"
#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/Utils.h"

namespace bw {
namespace core {
using namespace std;

VertexTransformer::VertexTransformer()
    : mFollowOrbitAngle(false), mCacheStaticness(false), mIsStatic(false), mAnimators{string("Scale"), string("Angle"), string("OrbitAngle"), string("OrbitDistance")} {
  auto initialiseAnimator = [this](Key key, array<float, 2> const& animationRange, float animationDefault) {
    auto& animator = mAnimators[(int)key];
    auto& animation = animator.getAnimationInterpolator();
    animation.setScale({0.0f, animationRange[0]}, {1.0f, animationRange[1]});
    animation.setDefaultStructure(
        {{0.0f, animationDefault}, {1.0f, animationDefault}},
        {{Easing::Linear}},
        true);

    auto& influence = animator.getInfluenceInterpolator();
    influence.setScale({0.0f, 0.0f}, {BW_INTERPOLATOR_MAX_DISTANCE, 1.0f});
    influence.setDefaultStructure(
        {{0.0f, 1.0f}, {BW_INTERPOLATOR_MAX_DISTANCE, 1.0f}},
        {{Easing::Linear}},
        true);
  };

  initialiseAnimator(Key::Scale, {1.0f, BW_INTERPOLATOR_MAX_SCALE}, 1.0f);
  initialiseAnimator(Key::Angle, {0.0f, BW_INTERPOLATOR_MAX_ANGLE}, 0.0f);
  initialiseAnimator(Key::OrbitAngle, {0.0f, BW_INTERPOLATOR_MAX_ANGLE}, 0.0f);
  initialiseAnimator(Key::OrbitDistance, {0.0f, BW_INTERPOLATOR_MAX_DISTANCE}, 100.0f);

  for (int i = 0; i < (int)Key::COUNT; ++i) {
    if (i == (int)Key::Scale) {
      mCurValues[i] = 0.5f;
    } else {
      mCurValues[i] = 0.0f;
    }

    mPrevValues[i] = -999991.0f;
  }
}

VertexTransformer::VertexTransformer(VertexTransformer const& other) {
  copyFrom(other);
}

VertexTransformer& VertexTransformer::operator=(VertexTransformer const& other) {
  copyFrom(other);
  return *this;
}

bool VertexTransformer::childrenModified() const {
  for (int i = 0; i < (int)Key::COUNT; ++i) {
    if (mAnimators[i].isModified()) {
      return true;
    }
  }

  return false;
}

void VertexTransformer::copyFrom(VertexTransformer const& other) {
  for (int i = 0; i < (int)Key::COUNT; ++i) {
    mAnimators[i] = other.mAnimators[i];
    mCurValues[i] = other.mCurValues[i];
    mPrevValues[i] = other.mPrevValues[i];
  }

  mFollowOrbitAngle = other.mFollowOrbitAngle;
  mCacheStaticness = other.mCacheStaticness;
  mIsStatic = other.mIsStatic;
}

void VertexTransformer::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("vertexTransformer");
  {
    serializer->writeBool("followOrbitAngle", mFollowOrbitAngle);

    string interpolators[(int)Key::COUNT] = {
        "scale", "angle", "orbitAngle", "orbitDistance"};

    serializer->beginArray("animators");
    {
      for (int i = 0; i < (int)Key::COUNT; ++i) {
        serializer->beginMap(interpolators[i]);
        {
          mAnimators[i].serialize(serializer, workData);
          serializer->endMap();
        }
      }

      serializer->endArray();  // animators
    }

    serializer->endMap();  // vertexTransformer
  }
}

bool VertexTransformer::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  AnimatedProperty animators[(int)Key::COUNT];
  bool followOrbitAngle;

  try {
    serializer->beginMap("vertexTransformer");
    {
      followOrbitAngle = serializer->readBool("followOrbitAngle");

      string interpolators[(int)Key::COUNT] = {
          "scale", "angle", "orbitAngle", "orbitDistance"};

      serializer->beginArray("animators");
      {
        int i = 0;

        while (serializer->nextArrayItem()) {
          if (i >= (int)Key::COUNT) {
            addDeserializationError("Too many animators in VertexTransformer");
            return false;
          }

          serializer->beginMap(interpolators[i]);
          {
            if (!animators[i].deserialize(serializer, workData)) {
              copyErrorsAndWarnings(&animators[i], true, true);
              return false;
            }

            i++;
            serializer->endMap();
          }
        }

        if (i != (int)Key::COUNT) {
          addDeserializationError("Expected 4 animators in VertexTransformer");
          return false;
        }

        serializer->endArray();  // animators
      }

      serializer->endMap();  // vertexTransformer
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mFollowOrbitAngle = followOrbitAngle;

  for (int i = 0; i < (int)Key::COUNT; ++i) {
    mAnimators[i] = animators[i];
  }

  return true;
}

bool VertexTransformer::isStatic() const {
  if (mCacheStaticness) {
    return mIsStatic;
  }

  for (int i = 0; i < (int)Key::COUNT; ++i) {
    if (!mAnimators[i].isStatic()) {
      return false;
    }
  }

  return true;
}

void VertexTransformer::cacheStaticness(bool cache) {
  if (cache) {
    mCacheStaticness = false;
    mIsStatic = isStatic();
  }

  mCacheStaticness = cache;
}

void VertexTransformer::resetAnimatorCaptures() {
  for (int i = 0; i < (int)Key::COUNT; ++i) {
    mAnimators[i].resetCapture();
  }
}

void VertexTransformer::resetAnimator(Key key) {
  mAnimators[(int)key].reset();
}

void VertexTransformer::removeTransform(vector<tTransform>& flow, uint32_t index) {
  if (index >= flow.size()) {
    throw CoreException("index out of range");
  }
  flow.erase(flow.begin() + index);
}

void VertexTransformer::setTransformOperand(Key key, uint32_t index, uint32_t operandIndex, tTransform::OperandType operand) {
  auto& tff = mAnimators[(int)key].getTransforms();
  auto& tf = tff[index];

  tf.operands[operandIndex] = operand;
}

void VertexTransformer::setTransformInput(Key key, uint32_t index, uint32_t inputIndex, InputType input) {
  auto& tff = mAnimators[(int)key].getTransforms();
  auto& tf = tff[index];

  tf.inputs[inputIndex] = input;
}

void VertexTransformer::setTransformConstant(Key key, uint32_t index, uint32_t constantIndex, float constant) {
  auto& tff = mAnimators[(int)key].getTransforms();
  auto& tf = tff[index];

  tf.constants[constantIndex] = constant;
}

void VertexTransformer::setTransformFnMultiplier(Key key, uint32_t index, uint32_t fnMulIndex, float value) {
  auto& tff = mAnimators[(int)key].getTransforms();
  auto& tf = tff[index];

  tf.fnMultipliers[fnMulIndex] = value;
}

void VertexTransformer::setTransformTriggerLineIndex(Key key, uint32_t index, uint32_t indexIndex, uint32_t value) {
  auto& tff = mAnimators[(int)key].getTransforms();
  auto& tf = tff[index];

  tf.indices[indexIndex] = value;
}

void VertexTransformer::setTransformOperation(Key key, uint32_t index, tTransform::Operation operation) {
  auto& tff = mAnimators[(int)key].getTransforms();
  auto& tf = tff[index];

  tf.operation = operation;
}

void VertexTransformer::addTransform(Key key, tTransform const& transform) {
  mAnimators[(int)key].getTransforms().push_back(transform);
}

void VertexTransformer::removeTransform(Key key, uint32_t index) {
  removeTransform(mAnimators[(int)key].getTransforms(), index);
}

void VertexTransformer::swapTransforms(Key key, uint32_t index1, uint32_t index2) {
  auto& transforms = mAnimators[(int)key].getTransforms();
  swap(transforms[index1], transforms[index2]);
}

void VertexTransformer::setScaleTransforms(vector<tTransform> const& transforms) {
  mAnimators[(int)Key::Scale].setTransforms(transforms);
}

vector<tTransform> const& VertexTransformer::getScaleTransforms() const {
  return mAnimators[(int)Key::Scale].getTransforms();
}

void VertexTransformer::setAngleTransforms(vector<tTransform> const& transforms) {
  mAnimators[(int)Key::Angle].setTransforms(transforms);
}

vector<tTransform> const& VertexTransformer::getAngleTransforms() const {
  return mAnimators[(int)Key::Angle].getTransforms();
}

void VertexTransformer::setOrbitAngleTransforms(vector<tTransform> const& transforms) {
  mAnimators[(int)Key::OrbitAngle].setTransforms(transforms);
}

vector<tTransform> const& VertexTransformer::getOrbitAngleTransforms() const {
  return mAnimators[(int)Key::OrbitAngle].getTransforms();
}

void VertexTransformer::setOrbitDistanceTransforms(vector<tTransform> const& transforms) {
  mAnimators[(int)Key::OrbitDistance].setTransforms(transforms);
}

vector<tTransform> const& VertexTransformer::getOrbitDistanceTransforms() const {
  return mAnimators[(int)Key::OrbitDistance].getTransforms();
}

void VertexTransformer::updateTransformTriggerLineIndices(map<uint32_t, uint32_t> const& mapping) {
  for (int i = 0; i < (int)Key::COUNT; ++i) {
    auto& transforms = mAnimators[i].getTransforms();

    for (auto& transform : transforms) {
      for (int j = 0; j < 2; ++j) {
        if (transform.operands[j] == bw::core::tTransform::OperandType::TriggerLine ||
            transform.operands[j] == bw::core::tTransform::OperandType::TriggerLineRed ||
            transform.operands[j] == bw::core::tTransform::OperandType::TriggerLineBlue) {
          auto mappedIndex = mapping.find(transform.indices[j]);
          if (mappedIndex != mapping.end()) {
            transform.indices[j] = mappedIndex->second;
          }
        }
      }
    }
  }
}

Interpolator<float> const& VertexTransformer::getAnimationInterpolator(Key key) const {
  return mAnimators[(int)key].getAnimationInterpolator();
}

Interpolator<float>& VertexTransformer::getAnimationInterpolator(Key key) {
  return mAnimators[(int)key].getAnimationInterpolator();
}

Interpolator<float> const& VertexTransformer::getInfluenceInterpolator(Key key) const {
  return mAnimators[(int)key].getInfluenceInterpolator();
}

Interpolator<float>& VertexTransformer::getInfluenceInterpolator(Key key) {
  return mAnimators[(int)key].getInfluenceInterpolator();
}

void VertexTransformer::setFollowOrbitAngle(bool follow) {
  mFollowOrbitAngle = follow;
}

bool VertexTransformer::getFollowOrbitAngle() const {
  return mFollowOrbitAngle;
}

void VertexTransformer::setCaptureMode(Key key, ValueCaptureMode mode) {
  mAnimators[(int)key].setCaptureMode(mode);
}

ValueCaptureMode VertexTransformer::getCaptureMode(Key key) const {
  return mAnimators[(int)key].getCaptureMode();
}

float VertexTransformer::getCurCapturedValue(Key key) const {
  return mAnimators[(int)key].getCurCapturedValue();
}

float VertexTransformer::calculateAnimationValue(VertexTransformer::Key key, InputValue const& inputs, double globalTime, uint32_t* firedEvents) {
  auto influenceAmt = BW_INTERPOLATOR_MAX_DISTANCE - inputs.entityInfluenceDistance;

  float cap = mAnimators[(int)key].captureValue(transformT(key, inputs, globalTime));
  float value = getAnimationInterpolator(key).getValue(cap);
  float infl = getInfluenceInterpolator(key).getValue(influenceAmt);

  switch (key) {
    case Key::Angle:
    case Key::OrbitAngle:
    case Key::OrbitDistance:
      value *= infl;
      break;

    case Key::Scale:
      value = 0.5f + (value - 1.0f) * 0.5f * infl;
      break;

    default:
      throw CoreException("Bad VertexTransformer::Key");
  }

  // Update values
  mPrevValues[(int)key] = mCurValues[(int)key];
  mCurValues[(int)key] = value;

  // Check events
  auto const& events = mAnimators[(int)key].getEvents();

  for (auto const& event : events) {
    if (checkAnimatedPropertyEvent(event, mPrevValues[(int)key], value)) {
      *firedEvents |= event.eventType;
    }
  }

  return value;
}

bool VertexTransformer::checkAnimatedPropertyEvent(AnimatedPropertyEvent const& event, float oldValue, float newValue) const {
  if (oldValue < -999990.0f) {
    return false;
  }

  bool crossedUp = oldValue < event.value && newValue >= event.value;
  bool crossedDown = oldValue > event.value && newValue <= event.value;

  switch (event.triggerType) {
    case AnimatedPropertyEventTriggerType::UpDown:
      return crossedUp || crossedDown;

    case AnimatedPropertyEventTriggerType::Up:
      return crossedUp;

    case AnimatedPropertyEventTriggerType::Down:
      return crossedDown;

    default:
      throw CoreException("Unhandled AnimatedPropertyEventTriggerType");
  }
}

float VertexTransformer::captureAndCacheAnimationValue(VertexTransformer::Key key, bool* cacheChanged) const {
  float value = mCurValues[(int)key];

  if (cacheChanged && value != mPrevValues[(int)key]) {
    *cacheChanged = true;
  }

  return value;
}

uint32_t VertexTransformer::getNumAnimatedPropertyEvents(Key key) const {
  return mAnimators[(int)key].getNumEvents();
}

vector<AnimatedPropertyEvent> const& VertexTransformer::getAnimatedPropertyEvents(Key key) const {
  return mAnimators[(int)key].getEvents();
}

void VertexTransformer::addAnimatedPropertyEvent(Key key, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value) {
  mAnimators[(int)key].addEvent(eventType, triggerType, value);
}

void VertexTransformer::removeAnimatedPropertyEvent(Key key, uint32_t index) {
  mAnimators[(int)key].removeEvent(index);
}

void VertexTransformer::updateAnimatedPropertyEvent(Key key, uint32_t index, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value) {
  mAnimators[(int)key].updateEvent(index, eventType, triggerType, value);
}

wp::Vector2 VertexTransformer::transformVertex(wp::Vector2 const& v, wp::Vector2 const& objectPosition, wp::Vector2 const& transformOffset, float orientation, InputValue const& inputs, bool* cacheChanged) const {
  if (cacheChanged) {
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

float VertexTransformer::transformT(Key key, InputValue const& inputs, double time) const {
  return mAnimators[(int)key].transformT(inputs, time);
}

}  // namespace core
}  // namespace bw