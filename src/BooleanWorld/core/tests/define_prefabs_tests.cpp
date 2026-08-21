#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireCoreException(Function&& function, char const* message) {
  try {
    function();
  } catch (bw::core::CoreException const&) {
    return;
  }
  throw std::runtime_error(message);
}

bw::core::RectanglePolygon* makeRectangle(float x) {
  auto* rectangle = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  rectangle->setSize(10.0f, 10.0f);
  rectangle->setPosition({x, 0.0f});
  return rectangle;
}

bw::core::DefinePrefabs* addDefinePrefabs(bw::core::Layer& layer) {
  auto* step = new bw::core::DefinePrefabs();
  layer.addStep(step);
  return step;
}

class ObservingStep final : public bw::core::LayerBuildStep {
public:
  mutable std::vector<bw::core::Primitive*> observed;

  std::string getType() const override { return "ObservingStep"; }
  bool mayBeFirstStep() const override { return false; }
  bw::core::LayerBuildStep* copy(
      std::map<bw::core::VertexTransformerObject const*,
               bw::core::VertexTransformerObject*>&) const override {
    return new ObservingStep();
  }
  void execute(bw::core::LayerBuildContext& context) const override {
    observed = context.getBuildPrimitives();
  }
  bool primitivesParticipateInBuild() const override { return true; }
  bool permitsDirectPrimitiveEditing() const override { return false; }
  bool acceptsNewPrimitives() const override { return false; }
  uint32_t adoptPrimitive(bw::core::Primitive*) override {
    throw bw::core::CoreException("ObservingStep owns no Primitives");
  }
  void replacePrimitive(bw::core::Primitive*, bw::core::Primitive*) override {
    throw bw::core::CoreException("ObservingStep owns no Primitives");
  }
  bool ownsPrimitive(bw::core::Primitive const*) const override { return false; }

private:
  void serializeArgs(std::shared_ptr<bw::core::Serializer>,
                     bw::core::SerializationWorkData&) const override {}
  bool deserializeArgs(std::shared_ptr<bw::core::Serializer>,
                       bw::core::SerializationWorkData&) override {
    return true;
  }
};

void squareTilingHasTheCoreRotationAngleTable() {
  auto const angles = bw::core::prefabTilingRotationAngles(
      bw::core::PrefabTilingType::Square);
  require(angles.size() == 4 && angles[0] == 0.0f && angles[1] == 90.0f &&
              angles[2] == 180.0f && angles[3] == 270.0f,
          "Square tiling did not expose its four allowed rotation angles");
}

void prefabIdsNamesAndStepArgumentsBehaveAsAuthoredData() {
  bw::core::DefinePrefabs step;
  require(step.getSelectedPrefab() == nullptr,
          "a new DefinePrefabs step selected a Prefab");
  require(step.getTilingType() == bw::core::PrefabTilingType::Square &&
              step.getSize() == 64.0f,
          "DefinePrefabs did not carry its square tiling arguments");

  auto* first = step.addPrefab("Duplicate");
  auto* deleted = step.addPrefab("Duplicate");
  auto const deletedId = deleted->getId();
  step.removePrefab(deleted);
  auto* replacement = step.addPrefab("Duplicate");

  require(first->getName() == replacement->getName(),
          "duplicate Prefab names were rewritten or rejected");
  require(first->getId() != replacement->getId() &&
              deletedId != replacement->getId(),
          "deleting the highest Prefab caused its id to be reused");

  step.setPrefabName(first, "Renamed");
  step.setSize(128.0f);
  require(first->getName() == "Renamed" && step.getSize() == 128.0f,
          "Prefab name or per-step tiling size was not mutable");
}

void selectionControlsOutputCapabilitiesAndLayerStorage() {
  bw::core::Layer layer(0, "Base", 256.0f, 16.0f);
  auto* step = addDefinePrefabs(layer);
  auto* first = step->addPrefab("First");
  auto* second = step->addPrefab("Second");
  auto const stepIndex = layer.getNumSteps() - 1;
  layer.setActiveStep(stepIndex);

  require(!step->permitsDirectPrimitiveEditing() &&
              !step->acceptsNewPrimitives() && layer.getNumPrimitives() == 0,
          "an unselected DefinePrefabs step exposed authoring or output");
  auto refused = std::unique_ptr<bw::core::Primitive>(makeRectangle(1.0f));
  requireCoreException([&] { layer.addPrimitive(refused.get()); },
                       "Layer accepted a Primitive with no Prefab selected");

  step->setSelectedPrefab(first);
  auto* firstPrimitive = makeRectangle(10.0f);
  layer.addPrimitive(firstPrimitive);
  require(step->permitsDirectPrimitiveEditing() &&
              step->acceptsNewPrimitives() &&
              first->getNumPrimitives() == 1 &&
              layer.getNumPrimitives() == 1 &&
              layer.getPrimitive(0) == firstPrimitive,
          "adding through Layer did not author into the selected Prefab");

  step->setSelectedPrefab(second);
  layer.rebuild();
  auto* secondPrimitive = makeRectangle(20.0f);
  layer.addPrimitive(secondPrimitive);
  require(layer.getNumPrimitives() == 1 &&
              layer.getPrimitive(0) == secondPrimitive &&
              first->getPrimitive(0) == firstPrimitive,
          "a selected Prefab emitted another Prefab's Primitives");

  auto* replacement = makeRectangle(30.0f);
  layer.replacePrimitive(0, replacement);
  require(second->getPrimitive(0) == replacement &&
              layer.getPrimitive(0) == replacement,
          "Layer replacement did not update selected Prefab storage");
  layer.removePrimitive(replacement);
  require(second->getNumPrimitives() == 0 && layer.getNumPrimitives() == 0,
          "Layer removal did not update selected Prefab storage");

  step->clearSelectedPrefab();
  layer.rebuild();
  require(layer.getNumPrimitives() == 0 &&
              !step->permitsDirectPrimitiveEditing() &&
              !step->acceptsNewPrimitives(),
          "clearing selection did not clear DefinePrefabs output and capabilities");
}

void laterStepsCannotObserveSelectedPrefabPrimitives() {
  bw::core::Layer layer(0, "Base", 256.0f, 16.0f);
  auto* step = addDefinePrefabs(layer);
  auto* prefab = step->addPrefab("Hidden from build");
  step->setSelectedPrefab(prefab);
  layer.setActiveStep(1);
  layer.addPrimitive(makeRectangle(10.0f));

  auto* observer = new ObservingStep();
  layer.addStep(observer);
  require(layer.getNumPrimitives() == 1,
          "selected Prefab Primitive was not available in the derived Layer cache");
  require(observer->observed.empty(),
          "a later build step observed a Prefab Primitive");
}

void layerCopyClonesPrefabsRemapsParentsAndClearsSelection() {
  auto source = std::make_unique<bw::core::Layer>(0, "Base", 256.0f, 16.0f);
  auto* sourceStep = addDefinePrefabs(*source);
  auto* sourcePrefab = sourceStep->addPrefab("Parented");
  sourceStep->setSelectedPrefab(sourcePrefab);
  source->setActiveStep(1);
  auto* root = makeRectangle(0.0f);
  auto* child = makeRectangle(10.0f);
  source->addPrimitive(root);
  source->addPrimitive(child);
  child->setParent(root);

  auto copy = std::make_unique<bw::core::Layer>(*source);
  auto* copiedStep = static_cast<bw::core::DefinePrefabs*>(copy->getStep(1));
  require(copiedStep->getSelectedPrefab() == nullptr &&
              copy->getNumPrimitives() == 0,
          "copying a Layer retained ephemeral Prefab selection");
  require(copiedStep->getNumPrefabs() == 1 &&
              copiedStep->getPrefab(0)->getPrimitive(0) != root &&
              copiedStep->getPrefab(0)->getPrimitive(1) != child,
          "copying a Layer did not deep-copy its Prefabs");

  auto* copiedRoot = copiedStep->getPrefab(0)->getPrimitive(0);
  auto* copiedChild = copiedStep->getPrefab(0)->getPrimitive(1);
  require(copiedChild->getParent() == copiedRoot,
          "a copied Prefab parent link was not remapped to the copied Primitive");
  source.reset();
  require(copiedChild->getParent() == copiedRoot,
          "a copied Prefab parent link still targeted the destroyed source Primitive");
}

void serializationRoundTripsPrefabsCounterAndArgumentsButNotSelection() {
  bw::core::Layer source(4, "Prefabs", 256.0f, 16.0f);
  auto* sourceStep = addDefinePrefabs(source);
  auto* kept = sourceStep->addPrefab("Same name");
  auto* deleted = sourceStep->addPrefab("Deleted");
  sourceStep->removePrefab(deleted);
  auto* second = sourceStep->addPrefab("Same name");
  auto const expectedNextId = second->getId() + 1;
  sourceStep->setSize(96.0f);
  sourceStep->setSelectedPrefab(kept);
  source.setActiveStep(1);
  source.addPrimitive(makeRectangle(42.0f));

  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData writeData;
  source.serialize(writer, writeData);
  writer->serialize();

  bw::core::Layer loaded;
  auto reader = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(writer->getSerializedString()));
  reader->deserialize();
  bw::core::SerializationWorkData readData;
  readData.accelGridSize = 16.0f;
  require(loaded.deserialize(reader, readData),
          "Layer containing DefinePrefabs failed to deserialize");

  auto* loadedStep = static_cast<bw::core::DefinePrefabs*>(loaded.getStep(1));
  require(loadedStep->getNumPrefabs() == 2 &&
              loadedStep->getPrefab(0)->getId() == kept->getId() &&
              loadedStep->getPrefab(0)->getName() == "Same name" &&
              loadedStep->getPrefab(0)->getNumPrimitives() == 1 &&
              loadedStep->getPrefab(0)->getPrimitive(0)->getPosition().x == 42.0f,
          "Prefab ids, names, or Primitives did not round-trip");
  require(loadedStep->getSelectedPrefab() == nullptr &&
              loaded.getNumPrimitives() == 0,
          "Prefab selection survived deserialization or emitted geometry on load");
  require(loadedStep->getTilingType() == bw::core::PrefabTilingType::Square &&
              loadedStep->getSize() == 96.0f,
          "DefinePrefabs tiling arguments did not round-trip");
  require(loadedStep->addPrefab("After load")->getId() == expectedNextId,
          "the serialized monotonic Prefab id counter was not restored");
}

void registryConstructsDefinePrefabsByTypeName() {
  auto const types = bw::core::LayerBuildStep::getRegisteredTypes();
  require(std::find(types.begin(), types.end(), "DefinePrefabs") != types.end(),
          "the step Registry did not enumerate DefinePrefabs");
  auto step = std::unique_ptr<bw::core::LayerBuildStep>(
      bw::core::LayerBuildStep::instantiate("DefinePrefabs"));
  require(step->getType() == "DefinePrefabs",
          "the step Registry did not construct DefinePrefabs");
}

}  // namespace

int main() {
  try {
    registryConstructsDefinePrefabsByTypeName();
    squareTilingHasTheCoreRotationAngleTable();
    prefabIdsNamesAndStepArgumentsBehaveAsAuthoredData();
    selectionControlsOutputCapabilitiesAndLayerStorage();
    laterStepsCannotObserveSelectedPrefabPrimitives();
    layerCopyClonesPrefabsRemapsParentsAndClearsSelection();
    serializationRoundTripsPrefabsCounterAndArgumentsButNotSelection();
    std::cout << "DefinePrefabs owns stable, serializable Prefabs while keeping their Primitives out of the build\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
