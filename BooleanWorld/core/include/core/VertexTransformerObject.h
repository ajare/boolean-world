#pragma once

#include <vector>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"
#include "core/VertexTransformer.h"
#include "core/Serializable.h"
#include "core/Interpolator.h"
#include "core/TransformFlow.h"
#include "core/InfluenceEye.h"
#include "core/InputType.h"
#include "core/WorldTriggerLine.h"

namespace bw {
namespace core {

class BW_API VertexTransformerObject : public Serializable {
  friend class World;

private:
  uint32_t mId;

  VertexTransformerObject* mParent;

  VertexTransformer mVertexTransformer;

  wp::Vector2 mPosition;

  wp::Vector2 mTransformOffset;

  float mOrientation;

  InfluenceEye mEye;

  wp::Vector2 mPrevEntityPosition;

  float mPrevEntityAngle;

  InputValue mInputs;

protected:
  bool childrenModified() const override;

  virtual void invalidatePostTransform(bool recalculateBounds, bool notifyWorld) const = 0;

  virtual void notifyWorldChanged() const = 0;

  virtual void copyFrom(VertexTransformerObject const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  VertexTransformer* getVertexTransformer();

  Interpolator<float>& getAnimationInterpolator(VertexTransformer::Key key);

  Interpolator<float>& getInfluenceInterpolator(VertexTransformer::Key key);

  wp::Vector2 calculateWorldPosition() const;

  uint32_t calculateAnimationValues(double time);

  wp::Vector2 transformVertex(wp::Vector2 const& v, bool* cacheChanged) const;

public:
  VertexTransformerObject();

  VertexTransformerObject(VertexTransformerObject const& other);

  VertexTransformerObject& operator=(VertexTransformerObject const& other);

  virtual ~VertexTransformerObject() = default;

  bool isStatic() const;

  void cacheStaticness(bool cache);

  void resetAnimatorCaptures();

  void resetAnimator(VertexTransformer::Key key);

  virtual void setId(uint32_t id);

  uint32_t getId() const;

  void setParent(VertexTransformerObject* parent);

  void setPosition(wp::Vector2 const& position);

  wp::Vector2 const& getPosition() const;

  void setTransformOffset(wp::Vector2 const& offset);

  wp::Vector2 const& getTransformOffset() const;

  void setOrientation(float orient);

  float getOrientation() const;

  void setFollowOrbitAngle(bool follow);

  bool getFollowOrbitAngle() const;

  //
  // Eyes
  //
  wp::Vector2 getInfluenceEyeOriginPosition() const;

  void setInfluenceEyeOriginOffset(wp::Vector2 const& originOffset);

  wp::Vector2 const& getInfluenceEyeOriginOffset() const;

  void setInfluenceEyeAngleOffset(float offset);

  float getInfluenceEyeAngleOffset() const;

  //
  // Animation interpolators
  //
  void setAnimationInterpolatorDefaultStructure(VertexTransformer::Key key, std::vector<Interpolator<float>::Point> const& points, std::vector<Interpolator<float>::Segment> const& segments, bool setToCurrent);

  Interpolator<float> const& getAnimationInterpolator(VertexTransformer::Key key) const;

  void setAnimationValues(VertexTransformer::Key key, std::vector<std::pair<float, float>> const& values);

  std::vector<Interpolator<float>::Point> const& getAnimationValues(VertexTransformer::Key key) const;

  uint32_t getNumAnimationValues(VertexTransformer::Key key) const;

  void updateAnimationValue(VertexTransformer::Key key, uint32_t index, float time, float const& value);

  void addAnimationValue(VertexTransformer::Key key, float time, float value);

  void removeAnimationValue(VertexTransformer::Key key, uint32_t index);

  float getAnimationValue(VertexTransformer::Key key, float t) const;

  void getAnimationScale(VertexTransformer::Key key, wp::Vector2* scaleMin, wp::Vector2* scaleMax);

  void setAnimationEasing(VertexTransformer::Key key, uint32_t segment, Easing easing);

  std::vector<Interpolator<float>::Segment> const& getAnimationSegments(VertexTransformer::Key key) const;

  std::vector<std::vector<Interpolator<float>::Point>> renderAnimation(VertexTransformer::Key key, float resolution) const;

  void addPointToAnimationInterpolator(VertexTransformer::Key key, float time, float value);

  void removePointFromAnimationInterpolator(VertexTransformer::Key key, uint32_t index);

  void updatePointInAnimationInterpolator(VertexTransformer::Key key, uint32_t index, float time, float value);

  void setAnimationInterpolatorEasing(VertexTransformer::Key key, uint32_t index, Easing easing);

  //
  // Influence interpolators
  //
  Interpolator<float> const& getInfluenceInterpolator(VertexTransformer::Key key) const;

  void setInfluenceValues(VertexTransformer::Key key, std::vector<std::pair<float, float>> const& values);

  std::vector<Interpolator<float>::Point> const& getInfluenceValues(VertexTransformer::Key key) const;

  uint32_t getNumInfluenceValues(VertexTransformer::Key key) const;

  void updateInfluenceValue(VertexTransformer::Key key, uint32_t index, float time, float const& value);

  void addInfluenceValue(VertexTransformer::Key key, float time, float value);

  void removeInfluenceValue(VertexTransformer::Key key, uint32_t index);

  float getInfluenceValue(VertexTransformer::Key key, float t) const;

  void getInfluenceScale(VertexTransformer::Key key, wp::Vector2* scaleMin, wp::Vector2* scaleMax);

  void setInfluenceEasing(VertexTransformer::Key key, uint32_t segment, Easing easing);

  std::vector<Interpolator<float>::Segment> const& getInfluenceSegments(VertexTransformer::Key key) const;

  std::vector<std::vector<Interpolator<float>::Point>> renderInfluence(VertexTransformer::Key key, float resolution) const;

  void addPointToInfluenceInterpolator(VertexTransformer::Key key, float time, float value);

  void removePointFromInfluenceInterpolator(VertexTransformer::Key key, uint32_t index);

  void updatePointInInfluenceInterpolator(VertexTransformer::Key key, uint32_t index, float time, float value);

  void setInfluenceInterpolatorEasing(VertexTransformer::Key key, uint32_t index, Easing easing);

  //
  // Transform flows
  //
  void setTransformOperand(VertexTransformer::Key key, uint32_t index, uint32_t operandIndex, tTransform::OperandType operand);

  void setTransformInput(VertexTransformer::Key key, uint32_t index, uint32_t inputIndex, InputType input);

  void setTransformConstant(VertexTransformer::Key key, uint32_t index, uint32_t constantIndex, float constant);

  void setTransformFnMultiplier(VertexTransformer::Key key, uint32_t index, uint32_t fnMulIndex, float value);

  void setTransformTriggerLineIndex(VertexTransformer::Key key, uint32_t index, uint32_t indexIndex, uint32_t value);

  void setTransformOperation(VertexTransformer::Key key, uint32_t index, tTransform::Operation operation);

  void addScaleTransform(tTransform const& transform);

  void removeScaleTransform(uint32_t index);

  void swapScaleTransforms(uint32_t index1, uint32_t index2);

  void setScaleTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getScaleTransforms() const;

  void addAngleTransform(tTransform const& transform);

  void removeAngleTransform(uint32_t index);

  void swapAngleTransforms(uint32_t index1, uint32_t index2);

  void setAngleTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getAngleTransforms() const;

  void addOrbitAngleTransform(tTransform const& transform);

  void removeOrbitAngleTransform(uint32_t index);

  void swapOrbitAngleTransforms(uint32_t index1, uint32_t index2);

  void setOrbitAngleTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getOrbitAngleTransforms() const;

  void addOrbitDistanceTransform(tTransform const& transform);

  void removeOrbitDistanceTransform(uint32_t index);

  void swapOrbitDistanceTransforms(uint32_t index1, uint32_t index2);

  void setOrbitDistanceTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform> const& getOrbitDistanceTransforms() const;

  void updateTransformTriggerLineIndices(std::map<uint32_t, uint32_t> const& mapping);

  //
  // Capture
  //
  void setCaptureMode(VertexTransformer::Key key, ValueCaptureMode mode);

  ValueCaptureMode getCaptureMode(VertexTransformer::Key key) const;

  float getCurCapturedValue(VertexTransformer::Key key) const;

  //
  // Events
  //
  uint32_t getNumAnimatedPropertyEvents(VertexTransformer::Key key) const;

  std::vector<AnimatedPropertyEvent> const& getAnimatedPropertyEvents(VertexTransformer::Key key) const;

  void addAnimatedPropertyEvent(VertexTransformer::Key key, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value);

  void removeAnimatedPropertyEvent(VertexTransformer::Key key, uint32_t index);

  void updateAnimatedPropertyEvent(VertexTransformer::Key key, uint32_t index, uint32_t eventType, AnimatedPropertyEventTriggerType triggerType, float value);

  //
  // Utility
  //
  void setInputUserValue(uint32_t index, float value);

  float getInputUserValue(uint32_t index) const;

  void setInputs(wp::Vector2 const& entityPosition, float entityAngle, std::vector<WorldTriggerLine*>* triggerLines);

  InputValue const& getInputs() const;

  float transformT(VertexTransformer::Key key, double time) const;
};

}  // namespace core
}  // namespace bw