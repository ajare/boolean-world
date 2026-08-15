#pragma once

#include "Platform.h"
#include "InputType.h"
#include "core/Serializable.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {

struct BW_API tTransform : public Serializable {
  enum struct OperandType {
    Input,
    Constant,
    Sine,
    InvCosine,
    Triangle,
    Saw,
    Square,
    TriggerLine,
    TriggerLineRed,
    TriggerLineBlue,
    TransformOutput,
    COUNT
  };

  enum struct Operation {
    Add,
    Mul,
    AbsDiff,
    Min,
    Max,
    Avg,
    Less,
    Greater,
    LessEq,
    GreaterEq,
    ModNormalise,
    COUNT
  };

  OperandType operands[2];
  float constants[2];
  float fnMultipliers[2];
  uint32_t indices[2];
  InputType inputs[2];
  Operation operation;

private:
  bool childrenModified() const override;

protected:
  void copyFrom(tTransform const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  tTransform();

  tTransform(
      OperandType operand0,
      OperandType operand1,
      float constant0,
      float constant1,
      float fnMul0,
      float fnMul1,
      uint32_t index0,
      uint32_t index1,
      InputType input0,
      InputType input1,
      Operation _operation);

  tTransform(tTransform const& other);

  tTransform& operator=(tTransform const& other);

  static tTransform makeConstant(float value);

  static tTransform makeZero();

  static tTransform makeOne();

  static tTransform makePassthroughPrevious();

  static tTransform makePassthroughInput(InputType inputType);
};

}  // namespace core
}  // namespace bw