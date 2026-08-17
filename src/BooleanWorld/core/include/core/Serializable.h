#pragma once

#include <vector>
#include <string>
#include <memory>

#include "Serializer.h"
#include "SerializationWorkData.h"

namespace bw {
namespace core {

class Serializable {
  std::vector<std::string> mDeserializationWarnings, mDeserializationErrors;

  mutable bool mModified;

private:
  virtual bool childrenModified() const = 0;

protected:
  virtual void copyFrom(Serializable const& other);

  void swapState(Serializable& other) noexcept;

  virtual void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const = 0;

  virtual bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) = 0;

  virtual void preSerialization(SerializationWorkData& workData) const;

  virtual void preDeserialization(SerializationWorkData& workData);

  virtual void postSerialization(SerializationWorkData& workData) const;

  virtual void postDeserialization(SerializationWorkData& workData);

  void copyErrorsAndWarnings(Serializable const* ser, bool errors, bool warnings);

  void addDeserializationWarning(std::string const& msg);

  void addDeserializationError(std::string const& msg);

  void modify();

public:
  virtual ~Serializable() = default;

  Serializable();

  Serializable(Serializable const& other);

  Serializable& operator=(Serializable const& other);

  std::vector<std::string> const& getDeserializationWarnings() const;

  std::vector<std::string> const& getDeserializationErrors() const;

  bool isModified() const;

  void serialize(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const;

  bool deserialize(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData);
};

}  // namespace core
}  // namespace bw