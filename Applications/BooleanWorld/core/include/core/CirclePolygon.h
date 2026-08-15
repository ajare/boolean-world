#pragma once

#include "core/Platform.h"
#include "core/RegularPolygon.h"

namespace bw {
namespace core {

class BW_API CirclePolygon : public RegularPolygon {
  static const uint32_t BaseResolution = 64;

  float mResolution;

private:
  friend class World;  // Only World can call the default constructor (during deserialization)

protected:
  CirclePolygon();

  void copyFrom(CirclePolygon const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  CirclePolygon(Operation operation, FillRule fillType, float resolution);

  CirclePolygon(CirclePolygon const& other);

  CirclePolygon& operator=(CirclePolygon const& other);

  Primitive* copy() const override;

  std::string getType() const override;

  void setNumSides(uint32_t numSides) override;

  void setResolution(float resolution);

  float getResolution() const;
};

}  // namespace core
}  // namespace bw