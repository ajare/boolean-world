#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "willpower/common/Vector2.h"

#include "willpower/serialization/Platform.h"

namespace WP_NAMESPACE {
namespace serialization {

class WP_SERIALIZATION_API SerializationUtils {
public:
  static void writeInt8(std::ostream& fp, int8_t value);

  static int8_t readInt8(std::istream& fp);

  static void writeUint8(std::ostream& fp, uint8_t value);

  static uint8_t readUint8(std::istream& fp);

  static void writeInt16(std::ostream& fp, int16_t value);

  static int16_t readInt16(std::istream& fp);

  static void writeUint16(std::ostream& fp, uint16_t value);

  static uint16_t readUint16(std::istream& fp);

  static void writeInt32(std::ostream& fp, int32_t value);

  static int32_t readInt32(std::istream& fp);

  static void writeUint32(std::ostream& fp, uint32_t value);

  static uint32_t readUint32(std::istream& fp);

  static void writeInt64(std::ostream& fp, int64_t value);

  static int64_t readInt64(std::istream& fp);

  static void writeUint64(std::ostream& fp, uint64_t value);

  static uint64_t readUint64(std::istream& fp);

  static void writeReal32(std::ostream& fp, float value);

  static float readReal32(std::istream& fp);

  static void writeReal64(std::ostream& fp, double value);

  static double readReal64(std::istream& fp);

  static void writeVector2(std::ostream& fp, Vector2 const& value);

  static void writeVector2(std::ostream& fp, float x, float y);

  static Vector2 readVector2(std::istream& fp);

  static void writeString(std::ostream& fp, std::string const& value);

  static std::string readString(std::istream& fp, int32_t maxChars = -1);

  static void writeByteArray(std::ostream& fp, char const* buffer, uint32_t length);

  static char* readByteArray(std::istream& fp, uint32_t& length);
};

}  // namespace serialization
}  // namespace WP_NAMESPACE
