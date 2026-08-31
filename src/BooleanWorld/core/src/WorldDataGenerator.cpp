#include "core/WorldDataGenerator.h"

#include <algorithm>
#include <stdexcept>

#include "core/Defines.h"
#include "core/Utils.h"
#include "core/World.h"

namespace bw::core {
using namespace std;

vector<OrderedPrimitive> selectAndOrderPrimitiveEntries(
    World const& world,
    LayerSelection const& selection,
    PrimitiveFilter const& filter) {
  vector<OrderedPrimitive> result;
  uint64_t layerOrdinal = 0;

  for (auto const* layer : world.getLayers()) {
    if (!IsLayerSelected(selection, layer->getId())) {
      ++layerOrdinal;
      continue;
    }

    vector<Primitive*> layerPrimitives;
    for (auto* primitive : layer->getPrimitives()) {
      if (!filter || filter(*layer, primitive)) {
        layerPrimitives.push_back(primitive);
      }
    }
    stable_sort(
        layerPrimitives.begin(), layerPrimitives.end(),
        WorldDataGenerator::SortPrimitivesByGeneratedPriority());

    auto const layerPriority = layerOrdinal << 56;
    for (auto* primitive : layerPrimitives) {
      result.push_back(
          {primitive, layerPriority | primitive->getGeneratedPriority()});
    }
    ++layerOrdinal;
  }
  return result;
}

vector<Primitive*> selectAndOrderPrimitives(
    World const& world,
    LayerSelection const& selection,
    PrimitiveFilter const& filter) {
  auto entries = selectAndOrderPrimitiveEntries(world, selection, filter);
  vector<Primitive*> primitives;
  primitives.reserve(entries.size());
  transform(
      entries.begin(), entries.end(), back_inserter(primitives),
      [](OrderedPrimitive const& entry) { return entry.primitive; });
  return primitives;
}

WorldDataGenerator::WorldDataGenerator()
    : mViewTriangle{} {
}

WorldDataGenerator::~WorldDataGenerator() = default;

WorldDataGenerator::WorldDataGenerator(WorldDataGenerator const& other) {
  copyFrom(other);
}

WorldDataGenerator& WorldDataGenerator::operator=(
    WorldDataGenerator const& other) {
  copyFrom(other);
  return *this;
}

void WorldDataGenerator::copyFrom(WorldDataGenerator const& other) {
  mLayerSelection = other.mLayerSelection;
  mPrimitiveFilter = other.mPrimitiveFilter;
  mChipParametersResolver = other.mChipParametersResolver;
  mViewTriangle = other.mViewTriangle;
}

WorldDataGenerator* WorldDataGenerator::copyForWorld(World const* world) {
  BW_UNUSED(world);
  return copy();
}

void WorldDataGenerator::rebindToWorld(World const* world) {
  BW_UNUSED(world);
}

void WorldDataGenerator::setLayerSelection(
    LayerSelection const& selection) {
  if (selection.none()) {
    throw std::invalid_argument("layer selection must not be empty");
  }
  if (selection != mLayerSelection) {
    mLayerSelection = selection;
    handleLayerSelectionChanged();
  }
}

LayerSelection const& WorldDataGenerator::getLayerSelection() const {
  return mLayerSelection;
}

void WorldDataGenerator::setPrimitiveFilter(PrimitiveFilter filter) {
  mPrimitiveFilter = move(filter);
  handlePrimitiveFilterChanged();
}

PrimitiveFilter const& WorldDataGenerator::getPrimitiveFilter() const {
  return mPrimitiveFilter;
}

void WorldDataGenerator::refreshPrimitiveFilter() {
  handlePrimitiveFilterChanged();
}

void WorldDataGenerator::setChipParametersResolver(ChipParametersResolver resolver) {
  mChipParametersResolver = move(resolver);
  handleChipParametersResolverChanged();
}

ChipParametersResolver const& WorldDataGenerator::getChipParametersResolver() const {
  return mChipParametersResolver;
}

void WorldDataGenerator::setActiveLayer(uint32_t layerId) {
  setLayerSelection(SelectLayer(layerId));
}

void WorldDataGenerator::_resetLayerSelection(
    LayerSelection const& selection) {
  if (selection.none()) {
    throw std::invalid_argument("layer selection must not be empty");
  }
  mLayerSelection = selection;
}

void WorldDataGenerator::handleEvents(float frameTime, uint32_t events) {
  BW_UNUSED(frameTime);
  BW_UNUSED(events);
}

void WorldDataGenerator::handleLayerSelectionChanged() {
}

void WorldDataGenerator::handlePrimitiveFilterChanged() {
}

void WorldDataGenerator::handleChipParametersResolverChanged() {
}

void WorldDataGenerator::update(
    float frameTime,
    WorldUpdateData const& data,
    uint32_t events) {
  auto const v0 = data.entityPosition;
  auto const [v1, v2] = calculateFovTriangle(v0, data.entityAngle, data.entityViewDist, data.entityFov);
  mViewTriangle = {v0, v1, v2};
  handleEvents(frameTime, events);
}
}  // namespace bw::core
