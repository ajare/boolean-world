#include <cassert>
#include <fstream>
#include <format>

#include "core/YamlSerializer.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {
using namespace std;

YamlSerializer::YamlSerializer(bool serializing, string const& filepath, bool sourceIsFile)
    : mFilepath(filepath), mSourceIsFile(sourceIsFile), mSerializing(serializing) {
}

YamlSerializer* YamlSerializer::toFile(std::string const& filepath) {
  return new YamlSerializer(true, filepath, true);
}

YamlSerializer* YamlSerializer::toString() {
  return new YamlSerializer(true, "", false);
}

YamlSerializer* YamlSerializer::fromFile(std::string const& filepath) {
  return new YamlSerializer(false, filepath, true);
}

YamlSerializer* YamlSerializer::fromString(std::string const& text) {
  return new YamlSerializer(false, text, false);
}

string YamlSerializer::getSerializedString() const {
  if (!mSerializing) {
    throw SerializationException("YamlSerializer not set to serialize!");
  }

  return mEmitter.c_str();
}

string YamlSerializer::getPath(string const& leaf) const {
  string path = "/";

  auto p = mPath;
  std::vector<string> s;

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

void YamlSerializer::writeUint8(string const& name, uint8_t value) {
  write(name, value);
}

void YamlSerializer::writeUint16(string const& name, uint16_t value) {
  write(name, value);
}

void YamlSerializer::writeUint32(string const& name, uint32_t value) {
  write(name, value);
}

void YamlSerializer::writeUint64(string const& name, uint64_t value) {
  write(name, value);
}

void YamlSerializer::writeInt8(string const& name, int8_t value) {
  write(name, value);
}

void YamlSerializer::writeInt16(string const& name, int16_t value) {
  write(name, value);
}

void YamlSerializer::writeInt32(string const& name, int32_t value) {
  write(name, value);
}

void YamlSerializer::writeInt64(string const& name, int64_t value) {
  write(name, value);
}

void YamlSerializer::writeFloat(string const& name, float value) {
  write(name, value);
}

void YamlSerializer::writeDouble(string const& name, double value) {
  write(name, value);
}

void YamlSerializer::writeVector2(string const& name, wp::Vector2 const& value) {
  beginArray(name, false);
  write("x", value.x);
  write("y", value.y);
  endArray();
}

void YamlSerializer::writeString(string const& name, char const* text, size_t length) {
  string value(text, length);
  write(name, value);
}

void YamlSerializer::beginMap(string const& name) {
  mPath.push(name);

  if (mSerializing) {
    if (!mTypeStack.empty()) {
      switch (mTypeStack.top()) {
        case ObjectType::Scalar:
          break;

        case ObjectType::Sequence:
          break;

        case ObjectType::Map:
          mEmitter << YAML::Key << name << YAML::Value;
      }
    }

    mTypeStack.push(ObjectType::Map);
    mEmitter << YAML::BeginMap;
  } else {
    auto node = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;

    if (node.IsSequence()) {
      auto index = mSeqIteratorStack.top();

      mNodeStack.push(node[index]);
    } else if (node.IsMap()) {
      if (!mNodeStack.empty()) {
        mNodeStack.push(node[name]);
      } else {
        mNodeStack.push(node);
      }
    } else {
      throw SerializationException(format("Unexpected node type: {}", (uint32_t)node.Type()));
    }
  }
}

void YamlSerializer::endMap() {
  if (mSerializing) {
    mTypeStack.pop();
    mEmitter << YAML::EndMap;
  } else {
    mNodeStack.pop();
  }

  mPath.pop();
}

void YamlSerializer::beginArray(string const& name, bool blockNotFlow) {
  mPath.push(name);

  if (mSerializing) {
    if (!mTypeStack.empty()) {
      switch (mTypeStack.top()) {
        case ObjectType::Scalar:
          break;

        case ObjectType::Sequence:
          break;

        case ObjectType::Map:
          mEmitter << YAML::Key << name << YAML::Value;
          break;
      }
    }

    mTypeStack.push(ObjectType::Sequence);
    mEmitter << (blockNotFlow ? YAML::Block : YAML::Flow) << YAML::BeginSeq;
  } else {
    auto node = !mNodeStack.empty() ? (name == "" ? mNodeStack.top() : mNodeStack.top()[name]) : mLoadedData;

    mNodeStack.push(node);
    mSeqIteratorStack.push(-1);
  }
}

void YamlSerializer::endArray() {
  if (mSerializing) {
    mTypeStack.pop();
    mEmitter << YAML::EndSeq;
  } else {
    mSeqIteratorStack.pop();
    mNodeStack.pop();
  }

  mPath.pop();
}

bool YamlSerializer::nextArrayItem() {
  auto& node = mNodeStack.top();
  auto& seqIt = mSeqIteratorStack.top();

  assert(node.IsSequence() && "YamlSerializer::nextArrayItem() - node is not a sequence");

  return ++seqIt != node.size();
}

void YamlSerializer::serialize() {
  if (!mSerializing) {
    throw SerializationException("YamlSerializer not set to serialize!");
  }

  if (mSourceIsFile) {
    ofstream fout(mFilepath);
    fout << mEmitter.c_str();
  }
}

void YamlSerializer::deserialize() {
  if (mSerializing) {
    throw SerializationException("YamlSerializer not set to deserialize!");
  }

  mLoadedData = mSourceIsFile ? YAML::LoadFile(mFilepath) : YAML::Load(mFilepath.c_str());
}

uint8_t YamlSerializer::readUint8(string const& name, bool optional, uint8_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<uint8_t>() : topNode[name].as<uint8_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint8 at {}", getPath(name), e.what()));
    }
  }
}

uint16_t YamlSerializer::readUint16(string const& name, bool optional, uint16_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<uint16_t>() : topNode[name].as<uint16_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint16 at {}", getPath(name), e.what()));
    }
  }
}

uint32_t YamlSerializer::readUint32(string const& name, bool optional, uint32_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<uint32_t>() : topNode[name].as<uint32_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint32 at {}", getPath(name), e.what()));
    }
  }
}

uint64_t YamlSerializer::readUint64(string const& name, bool optional, uint64_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<uint64_t>() : topNode[name].as<uint64_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read uint64 at {}", getPath(name), e.what()));
    }
  }
}

int8_t YamlSerializer::readInt8(string const& name, bool optional, int8_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<int8_t>() : topNode[name].as<int8_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int8 at {}", getPath(name), e.what()));
    }
  }
}

int16_t YamlSerializer::readInt16(string const& name, bool optional, int16_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<int16_t>() : topNode[name].as<int16_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int16 at {}", getPath(name), e.what()));
    }
  }
}

int32_t YamlSerializer::readInt32(string const& name, bool optional, int32_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<int32_t>() : topNode[name].as<int32_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int32 at {}", getPath(name), e.what()));
    }
  }
}

int64_t YamlSerializer::readInt64(string const& name, bool optional, int64_t defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<int64_t>() : topNode[name].as<int64_t>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read int64 at {}", getPath(name), e.what()));
    }
  }
}

float YamlSerializer::readFloat(string const& name, bool optional, float defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<float>() : topNode[name].as<float>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read float at {}", getPath(name), e.what()));
    }
  }
}

wp::Vector2 YamlSerializer::readVector2(string const& name, bool optional, wp::Vector2 const& defaultValue) {
  try {
    auto const& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    auto const node = name == "" ? topNode[mSeqIteratorStack.top()] : topNode[name];

    if (!node.IsSequence() || node.size() != 2) {
      throw SerializationException("Expected exactly two numeric values");
    }

    try {
      return wp::Vector2(node[0].as<float>(), node[1].as<float>());
    } catch (YAML::Exception const&) {
      throw SerializationException("Expected exactly two numeric values");
    }
  } catch (exception const& e) {
    if (optional) {
      return defaultValue;
    }

    throw SerializationException(format("Could not read Vector2 at {}: {}", getPath(name), e.what()));
  }
}

double YamlSerializer::readDouble(string const& name, bool optional, double defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<double>() : topNode[name].as<double>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read double at {}", getPath(name), e.what()));
    }
  }
}

string YamlSerializer::readString(string const& name, bool optional, std::string const& defaultValue) {
  try {
    auto& topNode = !mNodeStack.empty() ? mNodeStack.top() : mLoadedData;
    return name == "" ? topNode[mSeqIteratorStack.top()].as<string>() : topNode[name].as<string>();
  } catch (exception& e) {
    if (optional) {
      return defaultValue;
    } else {
      throw SerializationException(format("Could not read string at {}", getPath(name), e.what()));
    }
  }
}

}  // namespace core
}  // namespace bw