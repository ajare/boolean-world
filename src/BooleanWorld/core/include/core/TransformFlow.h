#pragma once

#include <vector>

#include "core/Platform.h"
#include "core/Interpolator.h"
#include "core/tTransform.h"
#include "core/InputValue.h"
#include "core/Serializable.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {
class TransformFlow : public Serializable {
  std::vector<tTransform> mTransforms;

private:
  bool childrenModified() const override;

  float triangle(double time, float value) const;

  float saw(double time, float value) const;

  float square(double time, float value) const;

protected:
  void copyFrom(TransformFlow const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  TransformFlow();

  TransformFlow(TransformFlow const& other);

  TransformFlow& operator=(TransformFlow const& other);

  float transformT(InputValue const& inputs, double time) const;

  void setTransforms(std::vector<tTransform> const& transforms);

  std::vector<tTransform>& getTransforms();

  std::vector<tTransform> const& getTransforms() const;
};

}  // namespace core
}  // namespace bw
