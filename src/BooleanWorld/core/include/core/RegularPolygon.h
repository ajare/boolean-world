#pragma once

#include "core/Platform.h"
#include "core/Primitive.h"

namespace bw {
namespace core {

class BW_API RegularPolygon : public Primitive {
  friend class Primitive;  // Only Primitive::instantiate calls the default constructor (during deserialization)

protected:
  uint32_t mNumSides;

protected:
  RegularPolygon();

  void copyFrom(RegularPolygon const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  std::vector<ComplexPolygon> generateVerticesImpl() override;

public:
  RegularPolygon(Operation operation, FillRule fillType, uint32_t numSides);

  RegularPolygon(RegularPolygon const& other);

  RegularPolygon& operator=(RegularPolygon const& other);

  Primitive* copy() const override;

  std::string getType() const override;

  std::string getName() const override;

  float getRadius() const override;

  virtual void setNumSides(uint32_t numSides);

  uint32_t getNumSides() const;
};

}  // namespace core
}  // namespace bw