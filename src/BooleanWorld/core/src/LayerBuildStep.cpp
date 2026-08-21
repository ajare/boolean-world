#include "core/LayerBuildStep.h"

#include "core/DefinePrefabs.h"
#include "core/PrimitiveField.h"
#include "core/Registry.h"

namespace bw {
namespace core {

using namespace std;

LayerBuildStep::LayerBuildStep()
    : mId(~0u), mEnabled(true) {
}

Registry<LayerBuildStep> const& LayerBuildStep::registry() {
  static const Registry<LayerBuildStep> stepRegistry(
      "layer build step",
      {{"DefinePrefabs", []() { return new DefinePrefabs; }},
       {"PrimitiveField", []() { return new PrimitiveField; }}});

  return stepRegistry;
}

vector<string> LayerBuildStep::getRegisteredTypes() {
  return registry().getTypes();
}

LayerBuildStep* LayerBuildStep::instantiate(string const& type) {
  return registry().create(type);
}

void LayerBuildStep::copyFrom(LayerBuildStep const& other) {
  Serializable::copyFrom(other);

  mId = other.mId;
  mEnabled = other.mEnabled;
}

bool LayerBuildStep::childrenModified() const {
  return false;
}

void LayerBuildStep::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->writeUint32("id", mId);
  serializer->writeBool("enabled", mEnabled);

  serializeArgs(serializer, workData);
}

bool LayerBuildStep::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  mId = serializer->readUint32("id", !serializer->isPositional(), ~0u);
  mEnabled = serializer->readBool("enabled");

  return deserializeArgs(serializer, workData);
}

void LayerBuildStep::setId(uint32_t id) {
  mId = id;
}

uint32_t LayerBuildStep::getId() const {
  return mId;
}

void LayerBuildStep::setEnabled(bool enabled) {
  if (mEnabled == enabled) {
    return;
  }

  mEnabled = enabled;
  modify();
}

bool LayerBuildStep::isEnabled() const {
  return mEnabled;
}

}  // namespace core
}  // namespace bw
