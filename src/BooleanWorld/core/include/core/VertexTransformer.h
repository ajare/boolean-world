#pragma once

#include <algorithm>
#include <vector>

#include <willpower/common/Vector2.h>
#include <willpower/common/MathsUtils.h>

#include "core/Platform.h"
#include "core/Serializable.h"
#include "core/SerializationException.h"
#include "core/AnimatedProperty.h"
#include "core/TransformFlow.h"
#include "core/ValueCaptureMode.h"

namespace bw {
namespace core {
class BW_API VertexTransformer : public Serializable {
public:
  enum struct Key {
    Scale,
    Angle,
    OrbitAngle,
    OrbitDistance,
    COUNT
  };

private:
  AnimatedProperty mAnimators[(int)Key::COUNT];

  bool mFollowOrbitAngle;

  float mCurValues[(int)Key::COUNT];

  float mPrevValues[(int)Key::COUNT];

  bool mCacheStaticness, mIsStatic;

private:
  bool childrenModified() const override;

  bool checkAnimatedPropertyEvent(AnimatedPropertyEvent const& event, float oldValue, float newValue) const;

protected:
  void copyFrom(VertexTransformer const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  void removeTransform(std::vector<tTransform>& flow, uint32_t index);

  float captureAndCacheAnimationValue(VertexTransformer::Key key, bool* cacheChanged) const;

public:
  VertexTransformer();

  VertexTransformer(VertexTransformer const& other);

  VertexTransformer& operator=(VertexTransformer const& other);

  bool isStatic() const;

  void cacheStaticness(bool cache);

  void resetAnimatorCaptures();

  void resetAnimator(Key key);

  //
  // Transform flows
  //
  void setTransformOperand(Key key, uint32_t index, uint32_t operandIndex, tTransform::OperandType operand);

  void setTransformInput(Key key, uint32_t index, uint32_t inputIndex, InputType input);

  void setTransformConstant(Key key, uint32_t index, uint32_t constantIndex, float constant);

  void setTransformFnMultiplier(Key key, uint32_t index, uint32_t fnMulIndex, float value);

  void setTransformTriggerLineIndex(Key key, uint32_t index, uint32_t indexIndex, uint32_t value);

  void setTransformOperation(Key key, uint32_t index, tTransform::Operation operation);

  void addTransform(Key key, tTransform const& transform);

  void removeTransform(Key key, uint32_t index);

  void swapTransforms(Key key, uint32_t index1, uint32_t index2);

  void setScaleTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getScaleTransforms() const;

  void setAngleTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getAngleTransforms() const;

  void setOrbitAngleTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getOrbitAngleTransforms() const;

  void setOrbitDistanceTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getOrbitDistanceTransforms() const;

  void updateTransformTriggerLineIndices(std::map<uint32_t, uint32_t> const& mapping);

  void setFollowOrbitAngle(bool follow);

  bool getFollowOrbitAngle() const;

  //
  // Interpolators
  //
  Interpolator<float> const& getAnimationInterpolator(Key key) const;

  Interpolator<float>& getAnimationInterpolator(Key key);

  Interpolator<float> const& getInfluenceInterpolator(Key key) const;

  Interpolator<float>& getInfluenceInterpolator(Key key);

  //
  // Capture
  //
  void setCaptureMode(Key key, ValueCaptureMode mode);

  ValueCaptureMode getCaptureMode(Key key) const;

  float getCurCapturedValue(Key key) const;

  //
  // Events
  //
  uint32_t getNumAnimatedPropertyEvents(Key key) const;

  std::vector<AnimatedPropertyEvent> const& getAnimatedPropertyEvents(Key key) const;

  void addAnimatedPropertyEvent(Key key, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value);

  void removeAnimatedPropertyEvent(Key key, uint32_t index);

  void updateAnimatedPropertyEvent(Key key, uint32_t index, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value);

  //
  // Transform
  //
  wp::Vector2 transformVertex(wp::Vector2 const& v, wp::Vector2 const& objectPosition, wp::Vector2 const& transformOffset, float orientation, InputValue const& inputs, bool* cacheChanged) const;

  float transformT(Key key, InputValue const& inputs, double time) const;

  float calculateAnimationValue(VertexTransformer::Key key, InputValue const& inputs, double time, uint32_t* firedEvents);
};

}  // namespace core
}  // namespace bw