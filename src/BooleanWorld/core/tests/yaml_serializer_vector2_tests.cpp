#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/SerializationException.h>
#include <core/YamlSerializer.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::unique_ptr<bw::core::YamlSerializer> readYaml(std::string const& yaml) {
  auto serializer = std::unique_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(yaml));
  serializer->deserialize();
  return serializer;
}

void requireVectorEquals(wp::Vector2 const& actual, wp::Vector2 const& expected,
                         char const* message) {
  require(std::abs(actual.x - expected.x) < Epsilon &&
              std::abs(actual.y - expected.y) < Epsilon,
          message);
}

void malformedRequiredVectorsReportErrorsAndLeaveTheSerializerUsable() {
  for (std::string const& vector : {"[]", "[1]", "[1, 2, 3]", "[1, nope]"}) {
    auto serializer = readYaml("vector: " + vector + "\nafter: 42\n");

    try {
      serializer->readVector2("vector");
      throw std::runtime_error("a malformed required Vector2 was accepted");
    } catch (bw::core::SerializationException const& error) {
      auto const message = std::string(error.what());
      require(message.contains("Vector2") && message.contains("/vector"),
              "a malformed required Vector2 did not identify its field");
      require(message.contains("exactly two numeric values"),
              "a malformed Vector2 did not explain the required shape");
    }

    require(std::abs(serializer->readFloat("after") - 42.0f) < Epsilon,
            "a read after a malformed required Vector2 failed");
  }
}

void malformedOptionalVectorsReturnTheirSuppliedDefault() {
  wp::Vector2 const defaultValue(7.0f, -3.0f);

  for (std::string const& vector : {"[]", "[1]", "[1, 2, 3]", "[1, nope]"}) {
    auto serializer = readYaml("vector: " + vector + "\nafter: 42\n");

    requireVectorEquals(serializer->readVector2("vector", true, defaultValue), defaultValue,
                        "a malformed optional Vector2 did not return its supplied default");
    require(std::abs(serializer->readFloat("after") - 42.0f) < Epsilon,
            "a read after a malformed optional Vector2 failed");
  }
}

void twoElementVectorsRoundTrip() {
  std::string const path = "yaml_serializer_vector2_tests.yaml";
  wp::Vector2 const expected(1.25f, -2.5f);

  {
    auto serializer = std::unique_ptr<bw::core::YamlSerializer>(
        bw::core::YamlSerializer::toFile(path));
    serializer->beginMap("");
    serializer->writeVector2("vector", expected);
    serializer->endMap();
    serializer->serialize();
  }

  auto serializer = std::unique_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromFile(path));
  serializer->deserialize();
  requireVectorEquals(serializer->readVector2("vector"), expected,
                      "a two-element Vector2 did not round-trip");

  std::remove(path.c_str());
}

}  // namespace

int main() {
  try {
    malformedRequiredVectorsReportErrorsAndLeaveTheSerializerUsable();
    malformedOptionalVectorsReturnTheirSuppliedDefault();
    twoElementVectorsRoundTrip();
    std::cout << "YAML Vector2 deserialization validates shape and preserves serializer state\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
