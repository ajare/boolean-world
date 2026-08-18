#pragma once

#include <string>

#include <willpower/common/Vector2.h>

namespace bw {
namespace core {

class Serializer {
public:
  virtual ~Serializer() = default;

  // Keyed formats (e.g. YAML) look fields up by name, so a read for a field
  // that a writer never emitted simply misses and falls back to its default
  // without disturbing anything else. Positional formats (e.g. BinarySerializer)
  // have no such per-field marker: every read consumes the next bytes in the
  // stream regardless of name, so attempting to read a field that no writer
  // ever produced would instead misread the following field. Deserialization
  // code that reads a field kept only for compatibility with old keyed-format
  // files (and never written any more) must skip that read when this is true.
  virtual bool isPositional() const {
    return false;
  }

  // Serialization
  void writeBool(std::string const& name, bool value);

  virtual void writeUint8(std::string const& name, uint8_t value) = 0;

  virtual void writeUint16(std::string const& name, uint16_t value) = 0;

  virtual void writeUint32(std::string const& name, uint32_t value) = 0;

  virtual void writeUint64(std::string const& name, uint64_t value) = 0;

  virtual void writeInt8(std::string const& name, int8_t value) = 0;

  virtual void writeInt16(std::string const& name, int16_t value) = 0;

  virtual void writeInt32(std::string const& name, int32_t value) = 0;

  virtual void writeInt64(std::string const& name, int64_t value) = 0;

  virtual void writeFloat(std::string const& name, float value) = 0;

  virtual void writeDouble(std::string const& name, double value) = 0;

  virtual void writeVector2(std::string const& name, wp::Vector2 const& value) = 0;

  void writeString(std::string const& name, std::string const& value);

  virtual void writeString(std::string const& name, char const* text, size_t length) = 0;

  virtual void beginMap(std::string const& name) = 0;

  virtual void endMap() = 0;

  virtual void beginArray(std::string const& name = "", bool blockNotFlow = true) = 0;

  virtual void endArray() = 0;

  virtual bool nextArrayItem() = 0;

  virtual void serialize() = 0;

  // Deserialization
  virtual void deserialize() = 0;

  // Optional reads return defaultValue when their value is absent or malformed.

  bool readBool(std::string const& name);

  virtual uint8_t readUint8(std::string const& name = "", bool optional = false, uint8_t defaultValue = 0) = 0;

  virtual uint16_t readUint16(std::string const& name = "", bool optional = false, uint16_t defaultValue = 0) = 0;

  virtual uint32_t readUint32(std::string const& name = "", bool optional = false, uint32_t defaultValue = 0) = 0;

  virtual uint64_t readUint64(std::string const& name = "", bool optional = false, uint64_t defaultValue = 0) = 0;

  virtual int8_t readInt8(std::string const& name = "", bool optional = false, int8_t defaultValue = 0) = 0;

  virtual int16_t readInt16(std::string const& name = "", bool optional = false, int16_t defaultValue = 0) = 0;

  virtual int32_t readInt32(std::string const& name = "", bool optional = false, int32_t defaultValue = 0) = 0;

  virtual int64_t readInt64(std::string const& name = "", bool optional = false, int64_t defaultValue = 0) = 0;

  virtual float readFloat(std::string const& name = "", bool optional = false, float defaultValue = 0.0f) = 0;

  virtual double readDouble(std::string const& name = "", bool optional = false, double defaultValue = 0.0) = 0;

  virtual wp::Vector2 readVector2(std::string const& name = "", bool optional = false, wp::Vector2 const& defaultValue = {0.0f, 0.0f}) = 0;

  virtual std::string readString(std::string const& name = "", bool optional = false, std::string const& defaultValue = "") = 0;
};

}  // namespace core
}  // namespace bw