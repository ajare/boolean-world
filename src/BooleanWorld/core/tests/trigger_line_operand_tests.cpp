#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/TransformFlow.h>
#include <core/VertexTransformer.h>
#include <core/WorldTriggerLine.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void staleTriggerLineOperandsUseZero() {
  bw::core::WorldTriggerLine triggerLine;
  std::vector<bw::core::WorldTriggerLine*> triggerLines = {&triggerLine};
  bw::core::InputValue inputs;
  inputs.triggerLines = &triggerLines;

  std::vector<bw::core::tTransform::OperandType> const operandTypes = {
      bw::core::tTransform::OperandType::TriggerLine,
      bw::core::tTransform::OperandType::TriggerLineRed,
      bw::core::tTransform::OperandType::TriggerLineBlue,
  };

  for (auto operandType : operandTypes) {
    auto transform = bw::core::tTransform::makeConstant(1.0f);
    transform.operands[0] = operandType;

    bw::core::TransformFlow flow;
    flow.setTransforms({transform});
    require(flow.transformT(inputs, 0.0) == 0.0f,
            "a stale TriggerLine operand did not use the zero fallback");
  }
}

void triggerLineRemapSkipsUnmappedOperands() {
  auto transform = bw::core::tTransform::makeConstant(1.0f);
  transform.operands[0] = bw::core::tTransform::OperandType::TriggerLine;
  transform.indices[0] = 7;

  bw::core::VertexTransformer transformer;
  transformer.setScaleTransforms({transform});
  transformer.updateTransformTriggerLineIndices({});

  auto const& unchanged = transformer.getScaleTransforms()[0];
  require(unchanged.indices[0] == 7,
          "an unmapped TriggerLine operand was not skipped");

  transformer.updateTransformTriggerLineIndices({{7, 0}});
  auto const& remapped = transformer.getScaleTransforms()[0];
  require(remapped.indices[0] == 0,
          "a mapped TriggerLine operand was not remapped");
}

}  // namespace

int main() {
  try {
    staleTriggerLineOperandsUseZero();
    triggerLineRemapSkipsUnmappedOperands();
    std::cout << "Stale TriggerLine operands are handled safely\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
