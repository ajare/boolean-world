#include "core/LayerBuildStep.h"

#include "core/DefinePrefabs.h"
#include "core/PrimitiveField.h"
#include "core/PrefabField.h"
#include "core/Registry.h"

namespace bw {
namespace core {

using namespace std;

LayerBuildStep::LayerBuildStep()
    : mLayer(nullptr), mId(~0u), mEnabled(true), mFailed(false) {
}

Registry<LayerBuildStep>& LayerBuildStep::registry() {
  static Registry<LayerBuildStep> stepRegistry("layer build step");
  return stepRegistry;
}

vector<string> LayerBuildStep::getRegisteredTypes() {
  return registry().getTypes();
}

LayerBuildStep* LayerBuildStep::instantiate(string const& type) {
  return registry().create(type);
}

void LayerBuildStep::registerType(string const& type, Factory factory) {
  registry().registerType(type, move(factory));
}

void LayerBuildStep::registerCoreTypes() {
  registerType("DefinePrefabs", []() { return new DefinePrefabs; });
  registerType("PrefabField", []() { return new PrefabField; });
  registerType("PrimitiveField", []() { return new PrimitiveField; });
}

void LayerBuildStep::copyFrom(LayerBuildStep const& other) {
  Serializable::copyFrom(other);

  mId = other.mId;
  mEnabled = other.mEnabled;
  mName = other.mName;
}

bool LayerBuildStep::childrenModified() const {
  return false;
}

void LayerBuildStep::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->writeUint32("id", mId);
  serializer->writeBool("enabled", mEnabled);
  serializer->writeString("name", mName);

  serializeArgs(serializer, workData);
}

bool LayerBuildStep::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  mId = serializer->readUint32("id", !serializer->isPositional(), ~0u);
  mEnabled = serializer->readBool("enabled");
  mName = serializer->readString("name", true, "");

  return deserializeArgs(serializer, workData);
}

void LayerBuildStep::setId(uint32_t id) {
  mId = id;
}

void LayerBuildStep::bindLayer(Layer* layer) {
  if (layer == mLayer) {
    return;
  }

  auto* oldLayer = mLayer;
  mLayer = layer;
  owningLayerChanged(oldLayer, layer);
}

Layer* LayerBuildStep::getOwningLayer() const {
  return mLayer;
}

void LayerBuildStep::owningLayerChanged(Layer*, Layer*) {
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

void LayerBuildStep::setName(string const& name) {
  if (mName == name) {
    return;
  }

  mName = name;
  modify();
}

string const& LayerBuildStep::getName() const {
  return mName;
}

vector<string> LayerBuildStep::collectDependentResourceNames() const {
  return {};
}

void LayerBuildStep::clearFailure() const {
  mFailed = false;
  mFailureMessage.clear();
}

void LayerBuildStep::recordFailure(string message) const {
  mFailed = true;
  mFailureMessage = move(message);
}

bool LayerBuildStep::hasFailed() const {
  return mFailed;
}

string const& LayerBuildStep::getFailureMessage() const {
  return mFailureMessage;
}

}  // namespace core
}  // namespace bw
