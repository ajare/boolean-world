#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/BinarySerializer.h>
#include <core/SerializationException.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void scalarsAndStringsRoundTripThroughAMap() {
  std::string data;

  {
    auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
        bw::core::BinarySerializer::toString());
    serializer->beginMap("root");
    serializer->writeUint8("u8", 200);
    serializer->writeInt32("i32", -12345);
    serializer->writeFloat("f", 3.5f);
    serializer->writeDouble("d", -2.25);
    serializer->writeString("s", "hello world");
    serializer->writeVector2("v", wp::Vector2(1.5f, -4.0f));
    serializer->endMap();

    data = serializer->getSerializedString();
  }

  auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromString(data));
  serializer->deserialize();

  serializer->beginMap("root");
  require(serializer->readUint8("u8") == 200, "uint8 did not round-trip");
  require(serializer->readInt32("i32") == -12345, "int32 did not round-trip");
  require(std::abs(serializer->readFloat("f") - 3.5f) < Epsilon, "float did not round-trip");
  require(std::abs(serializer->readDouble("d") - (-2.25)) < Epsilon, "double did not round-trip");
  require(serializer->readString("s") == "hello world", "string did not round-trip");

  auto v = serializer->readVector2("v");
  require(std::abs(v.x - 1.5f) < Epsilon && std::abs(v.y - (-4.0f)) < Epsilon,
          "Vector2 did not round-trip");
  serializer->endMap();
}

void arraysOfScalarsRoundTrip() {
  std::vector<float> const values = {1.0f, 2.0f, 3.0f, 4.5f};
  std::string data;

  {
    auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
        bw::core::BinarySerializer::toString());
    serializer->beginArray("values");
    for (auto value : values) {
      serializer->writeFloat("", value);
    }
    serializer->endArray();

    data = serializer->getSerializedString();
  }

  auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromString(data));
  serializer->deserialize();

  std::vector<float> read;
  serializer->beginArray("values");
  while (serializer->nextArrayItem()) {
    read.push_back(serializer->readFloat());
  }
  serializer->endArray();

  require(read.size() == values.size(), "array of scalars did not preserve length");
  for (size_t i = 0; i < values.size(); ++i) {
    require(std::abs(read[i] - values[i]) < Epsilon, "array of scalars did not round-trip");
  }
}

void emptyArrayRoundTrips() {
  std::string data;

  {
    auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
        bw::core::BinarySerializer::toString());
    serializer->beginArray("values");
    serializer->endArray();

    data = serializer->getSerializedString();
  }

  auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromString(data));
  serializer->deserialize();

  serializer->beginArray("values");
  require(!serializer->nextArrayItem(), "an empty array reported an item");
  serializer->endArray();
}

void arraysOfMapsRoundTrip() {
  std::string data;

  {
    auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
        bw::core::BinarySerializer::toString());
    serializer->beginArray("items");
    for (int i = 0; i < 3; ++i) {
      serializer->beginMap("item");
      serializer->writeInt32("index", i);
      serializer->writeString("name", "item" + std::to_string(i));
      serializer->endMap();
    }
    serializer->endArray();

    data = serializer->getSerializedString();
  }

  auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromString(data));
  serializer->deserialize();

  int count = 0;
  serializer->beginArray("items");
  while (serializer->nextArrayItem()) {
    serializer->beginMap("item");
    require(serializer->readInt32("index") == count, "map array item index mismatch");
    require(serializer->readString("name") == "item" + std::to_string(count),
            "map array item name mismatch");
    serializer->endMap();
    count++;
  }
  serializer->endArray();

  require(count == 3, "array of maps did not preserve item count");
}

void fileRoundTripsPreserveContent() {
  std::string const path = "binary_serializer_tests.world";
  wp::Vector2 const expected(9.5f, -1.25f);

  {
    auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
        bw::core::BinarySerializer::toFile(path));
    serializer->beginMap("root");
    serializer->writeVector2("vector", expected);
    serializer->endMap();
    serializer->serialize();
  }

  auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromFile(path));
  serializer->deserialize();

  serializer->beginMap("root");
  auto v = serializer->readVector2("vector");
  serializer->endMap();

  require(std::abs(v.x - expected.x) < Epsilon && std::abs(v.y - expected.y) < Epsilon,
          "a Vector2 did not round-trip through a file");

  std::remove(path.c_str());
}

void malformedHeaderIsRejected() {
  auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromString("not a boolean world file"));

  try {
    serializer->deserialize();
    throw std::runtime_error("a malformed binary file was accepted");
  } catch (bw::core::SerializationException const&) {
    // Expected
  }
}

void truncatedOptionalReadReturnsDefault() {
  std::string data;

  {
    auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
        bw::core::BinarySerializer::toString());
    serializer->beginMap("root");
    serializer->writeUint8("present", 7);
    serializer->endMap();

    data = serializer->getSerializedString();
  }

  auto serializer = std::unique_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromString(data));
  serializer->deserialize();

  serializer->beginMap("root");
  require(serializer->readUint8("present") == 7, "a present field failed to read");
  require(serializer->readFloat("missing", true, 42.0f) == 42.0f,
          "a read past the end of the data did not return its supplied default");
}

}  // namespace

int main() {
  try {
    scalarsAndStringsRoundTripThroughAMap();
    arraysOfScalarsRoundTrip();
    emptyArrayRoundTrips();
    arraysOfMapsRoundTrip();
    fileRoundTripsPreserveContent();
    malformedHeaderIsRejected();
    truncatedOptionalReadReturnsDefault();
    std::cout << "BinarySerializer round-trips scalars, strings, arrays and maps\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
