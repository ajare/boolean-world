#pragma once

#include "core/Platform.h"
#include "core/Primitive.h"

namespace bw {
namespace core {

class BW_API SuperformulaPolygon : public Primitive {
  friend class World;  // Only World can call the default constructor (during deserialization)

private:
  float mResolution;

  float mValues[6];

private:
  float r(float theta) const;

  wp::Vector2 calculate(float theta) const;

protected:
  SuperformulaPolygon();

  void copyFrom(SuperformulaPolygon const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  std::vector<ComplexPolygon> generateVerticesImpl() override;

public:
  SuperformulaPolygon(Operation operation, FillRule fillType, float resolution, float values[6]);

  SuperformulaPolygon(SuperformulaPolygon const& other);

  SuperformulaPolygon& operator=(SuperformulaPolygon const& other);

  Primitive* copy() const override;

  std::string getType() const override;

  float getRadius() const override;

  void setResolution(float resolution);

  float getResolution() const;

  void setValue(uint32_t index, float value);

  float getValue(uint32_t index) const;
};

}  // namespace core
}  // namespace bw