#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/TransformFlow.h>
#include <core/Utils.h>
#include <core/YamlSerializer.h>
#include <core/tTransform.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string serializeTransform(bw::core::tTransform const& transform) {
  std::string const path = "transform_serialization_tests.yaml";
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toFile(path));
  bw::core::SerializationWorkData workData;
  transform.serialize(serializer, workData);
  serializer->serialize();

  std::ifstream file(path);
  std::string yaml((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  std::remove(path.c_str());
  return yaml;
}

bool deserializeTransform(std::string const& yaml, bw::core::tTransform* transform) {
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  serializer->deserialize();

  bw::core::SerializationWorkData workData;
  return transform->deserialize(serializer, workData);
}

std::string withValue(std::string yaml, std::string const& field,
                      std::string const& value) {
  auto const begin = yaml.find(field + ": ");
  require(begin != std::string::npos, "serialized transform is missing " + field);
  auto const valueBegin = begin + field.size() + 2;
  auto const valueEnd = yaml.find('\n', valueBegin);
  yaml.replace(valueBegin,
               (valueEnd == std::string::npos ? yaml.size() : valueEnd) - valueBegin,
               value);
  return yaml;
}

bool containsError(bw::core::tTransform const& transform,
                   std::string const& expected) {
  for (auto const& error : transform.getDeserializationErrors()) {
    if (error == expected) {
      return true;
    }
  }
  return false;
}

void malformedSerializedEnumsAreRejected() {
  auto yaml = serializeTransform(bw::core::tTransform::makeConstant(1.0f));

  struct InvalidField {
    char const* field;
    char const* value;
    char const* error;
  };
  std::vector<InvalidField> const invalidFields = {
      {"operand0", "11", "Invalid transform operand type: 11"},
      {"input0", "10", "Invalid transform input type: 10"},
      {"operation", "11", "Invalid transform operation: 11"},
  };

  for (auto const& invalid : invalidFields) {
    bw::core::tTransform transform;
    require(!deserializeTransform(withValue(yaml, invalid.field, invalid.value),
                                  &transform),
            std::string("invalid ") + invalid.field + " deserialized");
    require(containsError(transform, invalid.error),
            std::string("invalid ") + invalid.field + " did not name its value");
  }
}

void nonPositiveSerializedFunctionMultiplierIsRejected() {
  auto yaml = withValue(
      serializeTransform(bw::core::tTransform::makeConstant(1.0f)),
      "fnMultiplier0", "0");

  bw::core::tTransform transform;
  require(!deserializeTransform(yaml, &transform),
          "a zero function multiplier deserialized");
  require(containsError(transform, "Invalid transform function multiplier: 0"),
          "zero function multiplier did not name its value");
}

void invalidRuntimeValuesUseSafeFallbacks() {
  bw::core::InputValue inputs;
  bw::core::TransformFlow flow;

  auto invalidOperand = bw::core::tTransform::makeConstant(1.0f);
  invalidOperand.operands[0] = bw::core::tTransform::OperandType::COUNT;
  flow.setTransforms({invalidOperand});
  require(flow.transformT(inputs, 0.25) == 0.0f,
          "an invalid operand type did not use the zero fallback");

  auto invalidInput = bw::core::tTransform::makePassthroughInput(
      bw::core::InputType::COUNT);
  flow.setTransforms({invalidInput});
  require(flow.transformT(inputs, 0.25) == 0.0f,
          "an invalid input type did not use the zero fallback");

  auto invalidOperation = bw::core::tTransform::makeConstant(1.0f);
  invalidOperation.operation = bw::core::tTransform::Operation::COUNT;
  flow.setTransforms({invalidOperation});
  require(flow.transformT(inputs, 0.25) == 0.0f,
          "an invalid operation did not use the zero fallback");

  std::vector<bw::core::tTransform::OperandType> const waveforms = {
      bw::core::tTransform::OperandType::Sine,
      bw::core::tTransform::OperandType::InvCosine,
      bw::core::tTransform::OperandType::Triangle,
      bw::core::tTransform::OperandType::Saw,
      bw::core::tTransform::OperandType::Square,
  };
  for (auto waveform : waveforms) {
    auto transform = bw::core::tTransform::makeConstant(1.0f);
    transform.operands[0] = waveform;
    transform.fnMultipliers[0] = 0.0f;
    flow.setTransforms({transform});
    require(std::isfinite(flow.transformT(inputs, 0.25)),
            "a zero runtime function multiplier produced a non-finite value");
  }
}

void clampAngleHandlesNonFiniteValues() {
  require(bw::core::clamp_angle(std::numeric_limits<float>::quiet_NaN()) == 0.0f,
          "clamp_angle did not reject NaN");
  require(bw::core::clamp_angle(std::numeric_limits<float>::infinity()) == 0.0f,
          "clamp_angle did not reject infinity");
  require(std::abs(bw::core::clamp_angle(-720.5f) - 359.5f) < 0.0001f,
          "clamp_angle did not normalise a finite angle");
}

}  // namespace

int main() {
  try {
    malformedSerializedEnumsAreRejected();
    nonPositiveSerializedFunctionMultiplierIsRejected();
    invalidRuntimeValuesUseSafeFallbacks();
    clampAngleHandlesNonFiniteValues();
    std::cout << "Transform serialization validation coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
