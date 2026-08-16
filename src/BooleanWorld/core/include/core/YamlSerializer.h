#pragma once

#include <stack>

#include <yaml-cpp/yaml.h>

#include "Serializer.h"
#include "SerializerFactory.h"

namespace bw {
namespace core {

class YamlSerializer : public Serializer {
  enum struct ObjectType {
    Scalar,
    Sequence,
    Map
  };

private:
  bool mSerializing;

  std::string mFilepath;

  bool mSourceIsFile;

  YAML::Emitter mEmitter;

  YAML::Node mLoadedData;

  std::stack<ObjectType> mTypeStack;

  std::stack<YAML::Node> mNodeStack;

  std::stack<int> mSeqIteratorStack;

  std::stack<std::string> mPath;

private:
  YamlSerializer(bool serializing, std::string const& source, bool sourceIsFile);

  std::string getPath(std::string const& leaf) const;

  template <typename T>
  void write(std::string const& name, T const& value) {
    switch (mTypeStack.top()) {
      case ObjectType::Scalar:
      case ObjectType::Sequence:
        mEmitter << value;
        break;
      case ObjectType::Map:
        mEmitter << YAML::Key << name << YAML::Value << value;
        break;
    }
  }

public:
  static YamlSerializer* toFile(std::string const& filepath);

  static YamlSerializer* toString();

  static YamlSerializer* fromFile(std::string const& filepath);

  static YamlSerializer* fromString(std::string const& text);

  std::string getSerializedString() const;

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

class YamlSerializerFactory : public SerializerFactory {
  bool mSerializing;

  bool mIsFile;

public:
  YamlSerializerFactory(bool serializing, bool isFile)
      : SerializerFactory(), mSerializing(serializing), mIsFile(isFile) {
  }

  Serializer* create(std::string const& target) override {
    if (mSerializing) {
      return YamlSerializer::toFile(target);
    } else {
      return mIsFile ? YamlSerializer::fromFile(target) : YamlSerializer::fromString(target);
    }
  }

  std::string description() override {
    return "YAML";
  }

  std::string extension() override {
    return "yaml";
  }
};

}  // namespace core
}  // namespace bw