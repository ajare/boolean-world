#include <cassert>
#include <cstring>
#include <fstream>
#include <format>
#include <vector>

#include "core/BinarySerializer.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {
using namespace std;

namespace {
constexpr char kMagic[4] = {'B', 'W', 'L', 'D'};
constexpr uint32_t kFormatVersion = 1;
}  // namespace

BinarySerializer::BinarySerializer(bool serializing, string const& source, bool sourceIsFile)
    : mFilepath(source), mSourceIsFile(sourceIsFile), mSerializing(serializing), mReadPos(0) {
  if (mSerializing) {
    writeHeader();
  }
}

BinarySerializer* BinarySerializer::toFile(string const& filepath) {
  return new BinarySerializer(true, filepath, true);
}

BinarySerializer* BinarySerializer::toString() {
  return new BinarySerializer(true, "", false);
}

BinarySerializer* BinarySerializer::fromFile(string const& filepath) {
  return new BinarySerializer(false, filepath, true);
}

BinarySerializer* BinarySerializer::fromString(string const& data) {
  return new BinarySerializer(false, data, false);
}

string BinarySerializer::getSerializedString() const {
  if (!mSerializing) {
    throw SerializationException("BinarySerializer not set to serialize!");
  }

  return mBuffer;
}

string BinarySerializer::getPath(string const& leaf) const {
  string path = "/";

  auto p = mPath;
  vector<string> s;

  while (!p.empty()) {
    s.push_back(p.top());
    p.pop();
  }

  for (auto rit = s.rbegin(); rit != s.rend(); ++rit) {
    path += *rit;
    path += "/";
  }

  path += leaf;

  return path;
}

void BinarySerializer::writeHeader() {
  writeRaw(kMagic, sizeof(kMagic));
  writeRaw(&kFormatVersion, sizeof(kFormatVersion));
}

void BinarySerializer::readHeader() {
  char magic[sizeof(kMagic)];
  readRaw(magic, sizeof(magic));
  if (memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
    throw SerializationException("Not a valid BooleanWorld binary file");
  }

  uint32_t version;
  readRaw(&version, sizeof(version));
  if (version != kFormatVersion) {
    throw SerializationException(format("Unsupported binary world format version: {}", version));
  }
}

void BinarySerializer::writeRaw(void const* data, size_t size) {
  mBuffer.append(reinterpret_cast<char const*>(data), size);
}

void BinarySerializer::readRaw(void* data, size_t size) {
  if (mReadPos + size > mBuffer.size()) {
    throw SerializationException("Unexpected end of binary data");
  }

  memcpy(data, mBuffer.data() + mReadPos, size);
  mReadPos += size;
}

void BinarySerializer::beginValue() {
  if (!mTypeStack.empty() && mTypeStack.top() == ObjectType::Sequence) {
    uint8_t const marker = 1;
    writeRaw(&marker, sizeof(marker));
  }
}

void BinarySerializer::writeUint8(string const& name, uint8_t value) {
  write(name, value);
}

void BinarySerializer::writeUint16(string const& name, uint16_t value) {
  write(name, value);
}

void BinarySerializer::writeUint32(string const& name, uint32_t value) {
  write(name, value);
}

void BinarySerializer::writeUint64(string const& name, uint64_t value) {
  write(name, value);
}

void BinarySerializer::writeInt8(string const& name, int8_t value) {
  write(name, value);
}

void BinarySerializer::writeInt16(string const& name, int16_t value) {
  write(name, value);
}

void BinarySerializer::writeInt32(string const& name, int32_t value) {
  write(name, value);
}

void BinarySerializer::writeInt64(string const& name, int64_t value) {
  write(name, value);
}

void BinarySerializer::writeFloat(string const& name, float value) {
  write(name, value);
}

void BinarySerializer::writeDouble(string const& name, double value) {
  write(name, value);
}

void BinarySerializer::writeVector2(string const& name, wp::Vector2 const& value) {
  (void)name;
  beginValue();
  writeRaw(&value.x, sizeof(float));
  writeRaw(&value.y, sizeof(float));
}

void BinarySerializer::writeString(string const& name, char const* text, size_t length) {
  (void)name;
  beginValue();

  uint32_t const len = (uint32_t)length;
  writeRaw(&len, sizeof(len));

  if (len > 0) {
    writeRaw(text, len);
  }
}

void BinarySerializer::beginMap(string const& name) {
  mPath.push(name);

  if (mSerializing) {
    beginValue();
  }

  mTypeStack.push(ObjectType::Map);
}

void BinarySerializer::endMap() {
  mTypeStack.pop();
  mPath.pop();
}

void BinarySerializer::beginArray(string const& name, bool blockNotFlow) {
  (void)blockNotFlow;

  mPath.push(name);

  if (mSerializing) {
    beginValue();
  }

  mTypeStack.push(ObjectType::Sequence);
}

void BinarySerializer::endArray() {
  if (mSerializing) {
    uint8_t const terminator = 0;
    writeRaw(&terminator, sizeof(terminator));
  }

  mTypeStack.pop();
  mPath.pop();
}

bool BinarySerializer::nextArrayItem() {
  try {
    uint8_t marker;
    readRaw(&marker, sizeof(marker));

    if (marker != 0 && marker != 1) {
      throw SerializationException("Corrupt array marker in binary data");
    }

    return marker == 1;
  } catch (exception& e) {
    throw SerializationException(format("Could not read array item at {}: {}", getPath(""), e.what()));
  }
}

void BinarySerializer::serialize() {
  if (!mSerializing) {
    throw SerializationException("BinarySerializer not set to serialize!");
  }

  if (mSourceIsFile) {
    ofstream fout(mFilepath, ios::binary);

    if (!fout) {
      throw SerializationException(format("Could not open {} for writing", mFilepath));
    }

    fout.write(mBuffer.data(), (streamsize)mBuffer.size());
  }
}

void BinarySerializer::deserialize() {
  if (mSerializing) {
    throw SerializationException("BinarySerializer not set to deserialize!");
  }

  if (mSourceIsFile) {
    ifstream fin(mFilepath, ios::binary);

    if (!fin) {
      throw SerializationException(format("Could not open {} for reading", mFilepath));
    }

    mBuffer.assign(istreambuf_iterator<char>(fin), istreambuf_iterator<char>());
  } else {
    mBuffer = mFilepath;
  }

  mReadPos = 0;

  readHeader();
}

uint8_t BinarySerializer::readUint8(string const& name, bool optional, uint8_t defaultValue) {
  try {
    return read<uint8_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint8 at {}: {}", getPath(name), e.what()));
    }
  }
}

uint16_t BinarySerializer::readUint16(string const& name, bool optional, uint16_t defaultValue) {
  try {
    return read<uint16_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint16 at {}: {}", getPath(name), e.what()));
    }
  }
}

uint32_t BinarySerializer::readUint32(string const& name, bool optional, uint32_t defaultValue) {
  try {
    return read<uint32_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint32 at {}: {}", getPath(name), e.what()));
    }
  }
}

uint64_t BinarySerializer::readUint64(string const& name, bool optional, uint64_t defaultValue) {
  try {
    return read<uint64_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint64 at {}: {}", getPath(name), e.what()));
    }
  }
}

int8_t BinarySerializer::readInt8(string const& name, bool optional, int8_t defaultValue) {
  try {
    return read<int8_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int8 at {}: {}", getPath(name), e.what()));
    }
  }
}

int16_t BinarySerializer::readInt16(string const& name, bool optional, int16_t defaultValue) {
  try {
    return read<int16_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int16 at {}: {}", getPath(name), e.what()));
    }
  }
}

int32_t BinarySerializer::readInt32(string const& name, bool optional, int32_t defaultValue) {
  try {
    return read<int32_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int32 at {}: {}", getPath(name), e.what()));
    }
  }
}

int64_t BinarySerializer::readInt64(string const& name, bool optional, int64_t defaultValue) {
  try {
    return read<int64_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int64 at {}: {}", getPath(name), e.what()));
    }
  }
}

float BinarySerializer::readFloat(string const& name, bool optional, float defaultValue) {
  try {
    return read<float>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read float at {}: {}", getPath(name), e.what()));
    }
  }
}

double BinarySerializer::readDouble(string const& name, bool optional, double defaultValue) {
  try {
    return read<double>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read double at {}: {}", getPath(name), e.what()));
    }
  }
}

wp::Vector2 BinarySerializer::readVector2(string const& name, bool optional, wp::Vector2 const& defaultValue) {
  try {
    float x = read<float>();
    float y = read<float>();
    return wp::Vector2(x, y);
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    }

    throw SerializationException(format("Could not read Vector2 at {}: {}", getPath(name), e.what()));
  }
}

string BinarySerializer::readString(string const& name, bool optional, string const& defaultValue) {
  try {
    uint32_t len;
    readRaw(&len, sizeof(len));

    if (mReadPos + len > mBuffer.size()) {
      throw SerializationException("Unexpected end of binary data");
    }

    string value(mBuffer.data() + mReadPos, len);
    mReadPos += len;

    return value;
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read string at {}: {}", getPath(name), e.what()));
    }
  }
}

}  // namespace core
}  // namespace bw
