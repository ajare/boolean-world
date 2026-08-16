#include "core/Serializable.h"

namespace bw {
namespace core {
using namespace std;

Serializable::Serializable()
    : mModified(true) {
}

Serializable::Serializable(Serializable const& other) {
  copyFrom(other);
}

Serializable& Serializable::operator=(Serializable const& other) {
  copyFrom(other);
  return *this;
}

void Serializable::copyFrom(Serializable const& other) {
  mDeserializationWarnings = other.mDeserializationWarnings;
  mDeserializationErrors = other.mDeserializationErrors;
  mModified = other.mModified;
}

void Serializable::addDeserializationWarning(string const& msg) {
  mDeserializationWarnings.push_back(msg);
}

void Serializable::addDeserializationError(string const& msg) {
  mDeserializationErrors.push_back(msg);
}

vector<string> const& Serializable::getDeserializationWarnings() const {
  return mDeserializationWarnings;
}

vector<string> const& Serializable::getDeserializationErrors() const {
  return mDeserializationErrors;
}

void Serializable::modify() {
  mModified = true;
}

bool Serializable::isModified() const {
  return childrenModified() || mModified;
}

void Serializable::copyErrorsAndWarnings(Serializable const* ser, bool errors, bool warnings) {
  if (warnings) {
    auto const& warnings = ser->getDeserializationWarnings();
    for (auto const& warning : warnings) {
      addDeserializationWarning(warning);
    }
  }

  if (errors) {
    auto const& errors = ser->getDeserializationErrors();
    for (auto const& error : errors) {
      addDeserializationError(error);
    }
  }
}

void Serializable::preSerialization(SerializationWorkData& workData) const {
}

void Serializable::preDeserialization(SerializationWorkData& workData) {
}

void Serializable::postSerialization(SerializationWorkData& workData) const {
}

void Serializable::postDeserialization(SerializationWorkData& workData) {
}

void Serializable::serialize(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  preSerialization(workData);
  serializeImpl(serializer, workData);
  postSerialization(workData);
  mModified = false;
}

bool Serializable::deserialize(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  preDeserialization(workData);

  mDeserializationWarnings.clear();
  mDeserializationErrors.clear();

  bool const deserialized = deserializeImpl(serializer, workData);

  postDeserialization(workData);

  mModified = false;
  return deserialized;
}

}  // namespace core
}  // namespace bw