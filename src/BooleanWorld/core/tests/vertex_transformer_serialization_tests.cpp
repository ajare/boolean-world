#include <cstdio>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/VertexTransformer.h>
#include <core/YamlSerializer.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string serializeVertexTransformer(bw::core::VertexTransformer const& transformer) {
  std::string const path = "vertex_transformer_serialization_tests.yaml";
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toFile(path));
  bw::core::SerializationWorkData workData;
  transformer.serialize(serializer, workData);
  serializer->serialize();

  std::ifstream file(path);
  std::string yaml((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  std::remove(path.c_str());
  return yaml;
}

bool deserializeVertexTransformer(std::string const& yaml,
                                  bw::core::VertexTransformer* transformer) {
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  serializer->deserialize();

  bw::core::SerializationWorkData workData;
  return transformer->deserialize(serializer, workData);
}

std::string withTooManyAnimators(std::string yaml) {
  auto const firstAnimator = yaml.find("- animatedProperty:");
  require(firstAnimator != std::string::npos,
          "serialized vertex transformer has no animator to duplicate");
  yaml.replace(firstAnimator, std::string("- animatedProperty:").size(),
               "- &animator animatedProperty:");
  yaml += "\n  - *animator\n";
  return yaml;
}

std::string withTooFewAnimators() {
  return "followOrbitAngle: 0\n"
         "animators: []\n";
}

bool containsError(bw::core::VertexTransformer const& transformer,
                   std::string const& expected) {
  for (auto const& error : transformer.getDeserializationErrors()) {
    if (error == expected) {
      return true;
    }
  }

  return false;
}

void deserializingExactAnimatorCountPreservesAnimatorRanges() {
  bw::core::VertexTransformer source;
  auto yaml = serializeVertexTransformer(source);

  bw::core::VertexTransformer target;
  require(deserializeVertexTransformer(yaml, &target),
          "a vertex transformer with four animators did not deserialize");

  wp::Vector2 scaleMin;
  wp::Vector2 scaleMax;
  target.getAnimationScale(bw::core::VertexTransformer::Key::Scale, &scaleMin,
                           &scaleMax);
  require(std::abs(scaleMin.y - 1.0f) < Epsilon,
          "a valid vertex transformer did not preserve its scale minimum");
  require(std::abs(scaleMax.y - 10.0f) < Epsilon,
          "a valid vertex transformer did not preserve its scale maximum");
}

void deserializingTooManyAnimatorsFailsWithoutWritingPastTheAnimatorArray() {
  bw::core::VertexTransformer source;
  auto yaml = withTooManyAnimators(serializeVertexTransformer(source));

  bw::core::VertexTransformer target;
  require(!deserializeVertexTransformer(yaml, &target),
          "a vertex transformer with too many animators deserialized");
  require(containsError(target, "Too many animators in VertexTransformer"),
          "too many animators did not report a deserialization error");
}

void deserializingTooFewAnimatorsFailsWithoutReplacingAnimatorDefaults() {
  auto yaml = withTooFewAnimators();

  bw::core::VertexTransformer target;
  require(!deserializeVertexTransformer(yaml, &target),
          "a vertex transformer with too few animators deserialized");
  require(containsError(target, "Expected 4 animators in VertexTransformer"),
          "too few animators did not report a deserialization error");

  wp::Vector2 scaleMin;
  wp::Vector2 scaleMax;
  target.getAnimationScale(bw::core::VertexTransformer::Key::Scale, &scaleMin,
                           &scaleMax);
  require(std::abs(scaleMin.y - 1.0f) < Epsilon,
          "too few animators replaced the scale minimum");
  require(std::abs(scaleMax.y - 10.0f) < Epsilon,
          "too few animators replaced the scale maximum");
}

}  // namespace

int main() {
  try {
    deserializingExactAnimatorCountPreservesAnimatorRanges();
    deserializingTooManyAnimatorsFailsWithoutWritingPastTheAnimatorArray();
    deserializingTooFewAnimatorsFailsWithoutReplacingAnimatorDefaults();
    std::cout << "Vertex transformer serialization validates animator counts\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
