#pragma once

#include <cstdint>
#include <stack>
#include <string>

#include "Serializer.h"
#include "SerializerFactory.h"

namespace bw {
namespace core {

// A compact binary Serializer, used to store Worlds as .world files.
//
// Unlike YamlSerializer, values are not addressed by name: fields are
// written and read back strictly in call order, so a given type's
// serializeImpl/deserializeImpl pair must read exactly the fields (in the
// same order) that it writes. This is already true of every Serializable
// in this codebase, and it lets the format skip storing any field names
// at all.
//
// Arrays don't know their length up front (callers just loop and write),
// so each array item is preceded by a one-byte "is there an item" marker
// (1 = item follows, 0 = end of array). nextArrayItem() consumes that
// marker while reading.
//
// Multi-byte values are written in host byte order. This engine only
// targets little-endian x86/x64, so no byte-swapping is performed.
class BinarySerializer : public Serializer {
  enum struct ObjectType {
    Sequence,
    Map
  };

private:
  bool mSerializing;

  std::string mFilepath;

  bool mSourceIsFile;

  std::string mBuffer;

  size_t mReadPos;

  std::stack<ObjectType> mTypeStack;

  std::stack<std::string> mPath;

private:
  BinarySerializer(bool serializing, std::string const& source, bool sourceIsFile);

  std::string getPath(std::string const& leaf) const;

  void writeHeader();

  void readHeader();

  void writeRaw(void const* data, size_t size);

  void readRaw(void* data, size_t size);

  // Emits the array-item presence marker when the value about to be
  // written is a direct item of an in-progress array.
  void beginValue();

  template <typename T>
  void write(std::string const& name, T const& value) {
    (void)name;
    beginValue();
    writeRaw(&value, sizeof(T));
  }

  template <typename T>
  T read() {
    T value{};
    readRaw(&value, sizeof(T));
    return value;
  }

public:
  static BinarySerializer* toFile(std::string const& filepath);

  static BinarySerializer* toString();

  static BinarySerializer* fromFile(std::string const& filepath);

  static BinarySerializer* fromString(std::string const& data);

  std::string getSerializedString() const;

  bool isPositional() const override {
    return true;
  }

  using Serializer::writeString;

  // Serialization
  void writeUint8(std::string const& name, uint8_t value) override;

  void writeUint16(std::string const& name, uint16_t value) override;

  void writeUint32(std::string const& name, uint32_t value) override;

  void writeUint64(std::string const& name, uint64_t value) override;

  void writeInt8(std::string const& name, int8_t value) override;

  void writeInt16(std::string const& name, int16_t value) override;

  void writeInt32(std::string const& name, int32_t value) override;

  void writeInt64(std::string const& name, int64_t value) override;

  void writeFloat(std::string const& name, float value) override;

  void writeDouble(std::string const& name, double value) override;

  void writeVector2(std::string const& name, wp::Vector2 const& value) override;

  void writeString(std::string const& name, char const* text, size_t length) override;

  void beginMap(std::string const& name) override;

  void endMap() override;

  void beginArray(std::string const& name = "", bool blockNotFlow = true) override;

  void endArray() override;

  bool nextArrayItem() override;

  void serialize() override;

  // Deserialization
  void deserialize() override;

  uint8_t readUint8(std::string const& name = "", bool optional = false, uint8_t defaultValue = 0) override;

  uint16_t readUint16(std::string const& name = "", bool optional = false, uint16_t defaultValue = 0) override;

  uint32_t readUint32(std::string const& name = "", bool optional = false, uint32_t defaultValue = 0) override;

  uint64_t readUint64(std::string const& name = "", bool optional = false, uint64_t defaultValue = 0) override;

  int8_t readInt8(std::string const& name = "", bool optional = false, int8_t defaultValue = 0) override;

  int16_t readInt16(std::string const& name = "", bool optional = false, int16_t defaultValue = 0) override;

  int32_t readInt32(std::string const& name = "", bool optional = false, int32_t defaultValue = 0) override;

  int64_t readInt64(std::string const& name = "", bool optional = false, int64_t defaultValue = 0) override;

  float readFloat(std::string const& name = "", bool optional = false, float defaultValue = 0.0f) override;

  double readDouble(std::string const& name = "", bool optional = false, double defaultValue = 0.0) override;

  wp::Vector2 readVector2(std::string const& name = "", bool optional = false, wp::Vector2 const& defaultValue = {0.0f, 0.0f}) override;

  std::string readString(std::string const& name = "", bool optional = false, std::string const& defaultValue = "") override;
};

class BinarySerializerFactory : public SerializerFactory {
  bool mSerializing;

  bool mIsFile;

public:
  BinarySerializerFactory(bool serializing, bool isFile)
      : SerializerFactory(), mSerializing(serializing), mIsFile(isFile) {
  }

  Serializer* create(std::string const& target) override {
    if (mSerializing) {
      return BinarySerializer::toFile(target);
    } else {
      return mIsFile ? BinarySerializer::fromFile(target) : BinarySerializer::fromString(target);
    }
  }

  std::string description() override {
    return "Binary World";
  }

  std::string extension() override {
    return "world";
  }
};

// Same wire format as BinarySerializerFactory, but for standalone Layer
// files rather than whole Worlds.
class LayerBinarySerializerFactory : public SerializerFactory {
  bool mSerializing;

  bool mIsFile;

public:
  LayerBinarySerializerFactory(bool serializing, bool isFile)
      : SerializerFactory(), mSerializing(serializing), mIsFile(isFile) {
  }

  Serializer* create(std::string const& target) override {
    if (mSerializing) {
      return BinarySerializer::toFile(target);
    } else {
      return mIsFile ? BinarySerializer::fromFile(target) : BinarySerializer::fromString(target);
    }
  }

  std::string description() override {
    return "Binary Layer";
  }

  std::string extension() override {
    return "layer";
  }
};

}  // namespace core
}  // namespace bw
