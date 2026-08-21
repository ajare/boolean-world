#include "core/LayerBuildStep.h"

#include "core/DefinePrefabs.h"
#include "core/PrimitiveField.h"
#include "core/Registry.h"

namespace bw {
namespace core {

using namespace std;

LayerBuildStep::LayerBuildStep()
    : mEnabled(true) {
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

  mEnabled = other.mEnabled;
}

bool LayerBuildStep::childrenModified() const {
  return false;
}

void LayerBuildStep::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->writeBool("enabled", mEnabled);

  serializeArgs(serializer, workData);
}

bool LayerBuildStep::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  mEnabled = serializer->readBool("enabled");

  return deserializeArgs(serializer, workData);
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
