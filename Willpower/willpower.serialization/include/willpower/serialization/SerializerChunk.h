#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "willpower/common/Platform.h"

#include "willpower/serialization/Platform.h"

namespace WP_NAMESPACE {
namespace serialization {

class WP_SERIALIZATION_API SerializerChunk {
  std::string mName;

  std::string mType;

  std::string mDescription;

  int mFileOffset;

  bool mOwnedBySerializer;

protected:
  virtual void writeBinaryImpl(std::ostream& fp) = 0;

  virtual void readBinaryImpl(std::istream& fp) = 0;

  virtual void writeTextImpl(std::ostream& fp) = 0;

  virtual void readTextImpl(std::istream& fp) = 0;

public:
  SerializerChunk(std::string const& name, std::string const& type, std::string const& description);

  virtual ~SerializerChunk() = default;

  std::string const& getName() const;

  std::string const& getType() const;

  std::string const& getDescription() const;

  void releaseOwnership();

  bool ownedBySerializer() const;

  int32_t writeBinary(std::ostream& fp);

  int32_t rewriteLastBinary(std::ostream& fp);

  int32_t readBinary(std::istream& fp);

  int32_t rereadLastBinary(std::istream& fp);

  void writeText(std::ostream& fp);

  void readText(std::istream& fp);
};

}  // namespace serialization
}  // namespace WP_NAMESPACE
