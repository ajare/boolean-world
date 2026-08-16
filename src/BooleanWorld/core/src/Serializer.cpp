#include "core/Serializer.h"

namespace bw {
namespace core {

using namespace std;

void Serializer::writeBool(string const& name, bool value) {
  writeInt8(name, value ? 1 : 0);
}

void Serializer::writeString(string const& name, string const& value) {
  writeString(name, value.c_str(), value.length());
}

bool Serializer::readBool(string const& name) {
  return readInt8(name) != 0;
}

}  // namespace core
}  // namespace bw