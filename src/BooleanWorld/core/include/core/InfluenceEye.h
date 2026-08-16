#pragma once

#include <array>
#include <vector>

#include "core/Platform.h"
#include "core/Serializable.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {

class BW_API InfluenceEye : public Serializable {
  wp::Vector2 mOriginOffset;

  float mAngleOffset;

  float mArcLength;

private:
  bool childrenModified() const override;

protected:
  void copyFrom(InfluenceEye const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  InfluenceEye();

  InfluenceEye(wp::Vector2 const& originOffset, float angleOffset, float arcLength);

  InfluenceEye(InfluenceEye const& other);

  InfluenceEye& operator=(InfluenceEye const& other);

  void setOriginOffset(wp::Vector2 const& originOffset);

  wp::Vector2 const& getOriginOffset() const;

  void setAngleOffset(float angleOffset);

  float getAngleOffset() const;

  void setArcLength(float arcLength);

  float getArcLength() const;

  bool inArc(wp::Vector2 const& position) const;
};

}  // namespace core
}  // namespace bw
