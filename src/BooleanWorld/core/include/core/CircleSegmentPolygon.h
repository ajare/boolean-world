#pragma once

#include "core/Platform.h"
#include "core/RegularPolygon.h"

namespace bw {
namespace core {

class BW_API CircleSegmentPolygon : public RegularPolygon {
  static const uint32_t BaseResolution = 64;

  float mArcLength;

  float mResolution;

private:
  friend class World;  // Only World can call the default constructor (during deserialization)

protected:
  CircleSegmentPolygon();

  void copyFrom(CircleSegmentPolygon const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  std::vector<ComplexPolygon> generateVerticesImpl() override;

public:
  CircleSegmentPolygon(Operation operation, FillRule fillType, float arcLength, float resolution);

  CircleSegmentPolygon(CircleSegmentPolygon const& other);

  CircleSegmentPolygon& operator=(CircleSegmentPolygon const& other);

  Primitive* copy() const override;

  std::string getType() const override;

  void setArcLength(float arcLength);

  float getArcLength() const;

  void setNumSides(uint32_t numSides) override;

  void setResolution(float resolution);

  float getResolution() const;
};

}  // namespace core
}  // namespace bw