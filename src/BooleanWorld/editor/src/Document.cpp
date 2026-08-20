#include <exception>
#include <filesystem>
#include <fstream>
#include <format>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <yaml-cpp/yaml.h>

#include "core/BinarySerializer.h"
#include "core/YamlSerializer.h"
#include "core/RegularPolygon.h"
#include "core/DynamicWorldDataGenerator.h"
#include "core/Vertex.h"
#include "core/MeshPrimitive.h"
#include "core/LayerBuildStep.h"

#include "common/GameDefines.h"

#include "Defines.h"
#include "Document.h"
#include "EditorException.h"
#include "AppHelpers.h"

extern spdlog::logger* gLogger;

namespace editor {
using namespace std;

Document* Document::msInstance = nullptr;

bool primitiveVisibleForActiveStep(
    bw::core::Layer const& layer,
    bw::core::Primitive const* primitive,
    Settings const& settings) {
  if (settings.showAllStepPrimitives) {
    return true;
  }

  if (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) {
    return true;
  }

  auto owningStepIndex = layer.getOwningStepIndex(primitive);

  return owningStepIndex == ~0u || owningStepIndex <= layer.getActiveStepIndex();
}

namespace {

// Selection's "current context" (the active Layer, and - unless
// showAllStepPrimitives opts out of the boundary - primitives no later than
// its active step) mirrors the world view's own visibility rule
// (editor::primitiveVisibleForActiveStep): a Primitive nothing draws should
// be nothing a click, a box, or Select All can pick up either. World's
// index-space is already scoped to the active Layer (World::getPrimitive et
// al forward to getActiveLayer()), so only the step boundary needs adding
// here.
set<uint32_t> getIgnoredPrimitiveIndices(bw::core::World const& world, Settings const& settings) {
  set<uint32_t> ignores;

  if (!settings.ghostActive) {
    ignores.insert(0);
  }

  auto* activeLayer = world.getActiveLayer();

  for (uint32_t i = 0; i < world.getNumPrimitives(); ++i) {
    auto* primitive = world.getPrimitive(i);

    if (!settings.renderAnimatedPrimitives && !primitive->isStatic()) {
      ignores.insert(i);
      continue;
    }

    // Mesh mode only exposes authored MeshPrimitives to the viewport. The
    // rest of the editor's objects remain visible but cannot receive a
    // hover, click, or box selection there.
    if (settings.mode == Settings::Mode::Mesh &&
        !dynamic_cast<bw::core::MeshPrimitive const*>(primitive)) {
      ignores.insert(i);
      continue;
    }

    if (activeLayer && !primitiveVisibleForActiveStep(*activeLayer, primitive, settings)) {
      ignores.insert(i);
      continue;
    }

    if (activeLayer) {
      auto owningStepIndex = activeLayer->getOwningStepIndex(primitive);
      if (owningStepIndex != ~0u &&
          !activeLayer->getStep(owningStepIndex)->permitsDirectPrimitiveEditing()) {
        ignores.insert(i);
      }
    }
  }

  return ignores;
}

}  // namespace

Document::Document()
    : mModified(false), mSelectedWorldVertexIndex(~0u), mSelectedTriggerLineIndex(~0u), mPlayerOldProxyPosition({0, 0}), mPlayerProxyPosition({0, 0}), mPlayerProxyAngle(0.0f), mPlayerOldProxyAngle(0.0f) {
}

Document::~Document() {
}

Document* Document::instance() {
  if (!msInstance) {
    msInstance = new Document();
  }

  return msInstance;
}

void Document::reset() {
  mModified = false;
  mFilepath = "";
  mWorld.reset();
  mSelectedWorldVertexIndex = ~0u;
  mSelectedTriggerLineIndex = ~0u;
  mSelectedPrimitiveIndices.clear();
  mPlayerProxyPosition.set(0.0f, 0.0f);
  mPlayerProxyAngle = 0.0f;
}

bool Document::isActive() const {
  return mWorld != nullptr;
}

void Document::setModified(bool modified) {
  mModified = modified;
}

bool Document::isModified() const {
  return mModified;
}

string const& Document::getFilepath() const {
  return mFilepath;
}

bool Document::hasFilepath() const {
  return mFilepath != "";
}

void Document::setWorld(bw::core::World const& world) {
  mWorld = make_shared<bw::core::World>(world);
}

WorldSnapshot Document::captureWorldSnapshot() const {
  if (!mWorld) {
    throw EditorException("Cannot snapshot an inactive document.");
  }

  auto serializer = shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  auto workData = bw::core::SerializationWorkData{};
  workData.markSerializedUnmodified = false;
  workData.includeGhostPrimitives = true;
  mWorld->serialize(serializer, workData);
  serializer->serialize();

  WorldSnapshot snapshot;
  snapshot.serializedWorld = serializer->getSerializedString();
  snapshot.accelerationGridSize = mWorld->getPrimitiveAccelerationGridSize();
  snapshot.alwaysUpdateWorldVertices = mWorld->getAlwaysUpdateVertices();

  auto generator = mWorld->getWorldDataGenerator();
  snapshot.layerSelection = generator->getLayerSelection();
  if (auto dynamicGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator const*>(generator)) {
    snapshot.hasDynamicGenerator = true;
    snapshot.alwaysUpdateGeneratorVertices = dynamicGenerator->getAlwaysUpdateVertices();
    snapshot.allowCommitIfVisible = dynamicGenerator->getAllowCommitIfVisible();
    snapshot.scheduledGenerationInterval = dynamicGenerator->getScheduledGenerationInterval();
  }

  return snapshot;
}

void Document::restoreWorldSnapshot(WorldSnapshot const& snapshot) {
  auto serializer = shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(snapshot.serializedWorld));
  serializer->deserialize();

  auto world = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);
  world->removePrimitive(uint32_t(ED_GHOST_INDEX));

  auto workData = bw::core::SerializationWorkData{};
  workData.accelGridSize = snapshot.accelerationGridSize;
  workData.allowEmptyWorld = true;
  if (!world->deserialize(serializer, workData)) {
    throw EditorException("Could not restore the world snapshot.");
  }

  world->setAlwaysUpdateVertices(snapshot.alwaysUpdateWorldVertices);
  auto generator = world->getWorldDataGenerator();
  generator->setLayerSelection(snapshot.layerSelection);
  if (snapshot.hasDynamicGenerator) {
    auto dynamicGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(generator);
    dynamicGenerator->setAlwaysUpdateVertices(snapshot.alwaysUpdateGeneratorVertices);
    dynamicGenerator->setAllowCommitIfVisible(snapshot.allowCommitIfVisible);
    dynamicGenerator->setScheduledGenerationInterval(snapshot.scheduledGenerationInterval);
  }

  mWorld = move(world);
}

void Document::setPrimitiveFilter(bw::core::PrimitiveFilter filter) {
  mPrimitiveFilter = move(filter);

  if (mWorld) {
    mWorld->getWorldDataGenerator()->setPrimitiveFilter(mPrimitiveFilter);
  }
}

shared_ptr<bw::core::World> Document::getWorld() {
  return mWorld;
}

void Document::setSelectedWorldVertexIndex(uint32_t index) {
  clearSelections();
  mSelectedWorldVertexIndex = index;
}

void Document::setSelectedTriggerLineIndex(uint32_t index) {
  clearSelections();
  mSelectedTriggerLineIndex = index;
}

void Document::setSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  clearSelections();
  mSelectedPrimitiveIndices = indices;
}

void Document::addSelectedPrimitiveIndex(uint32_t index) {
  mSelectedPrimitiveIndices.insert(index);
}

void Document::addSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  mSelectedPrimitiveIndices.insert(indices.begin(), indices.end());
}

void Document::removeSelectedPrimitiveIndex(uint32_t index) {
  mSelectedPrimitiveIndices.erase(index);
}

void Document::removeSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  for (auto index : indices) {
    mSelectedPrimitiveIndices.erase(index);
  }
}

void Document::clearSelections() {
  mSelectedPrimitiveIndices.clear();
  mSelectedWorldVertexIndex = ~0u;
  mSelectedTriggerLineIndex = ~0u;
}

DocumentHover Document::getHover(
    wp::Vector2 const& mouseWorldPos,
    Settings const& settings,
    bw::core::WorldData const* worldData) const {
  if (!isActive()) {
    return {};
  }

  if (settings.mode == Settings::Mode::Mesh) {
    auto primitiveIndices = getHoveredPrimitiveIndices(mouseWorldPos, settings);
    return primitiveIndices.empty()
               ? DocumentHover{}
               : DocumentHover{HoverableType::Primitive, std::move(primitiveIndices)};
  }

  if (worldData) {
    auto worldVertexIndex = static_cast<uint32_t>(
        worldData->getNearestVertexIndex(mouseWorldPos, 3.0f));
    if (worldVertexIndex != ~0u) {
      return {HoverableType::WorldVertex, {worldVertexIndex}};
    }
  }

  auto triggerLineIndex = getHoveredTriggerLineIndex(mouseWorldPos, settings);
  if (triggerLineIndex != ~0u) {
    return {HoverableType::TriggerLine, {triggerLineIndex}};
  }

  auto primitiveIndices = getHoveredPrimitiveIndices(mouseWorldPos, settings);
  if (!primitiveIndices.empty()) {
    return {HoverableType::Primitive, std::move(primitiveIndices)};
  }

  return {};
}

uint32_t Document::getHoveredPrimitiveIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  if (!isActive()) {
    return ~0u;
  }

  // Taken from the ordered list rather than from World::findPrimitiveIndex,
  // so the single hovered Primitive is the same one a click would select -
  // the ghost, where it overlaps something.
  auto hovered = getHoveredPrimitiveIndices(mouseWorldPos, settings);

  return hovered.empty() ? ~0u : hovered.front();
}

vector<uint32_t> Document::getHoveredPrimitiveIndices(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  if (!isActive()) {
    return {};
  }

  auto hovered = mWorld->findPrimitiveIndices(
      mouseWorldPos, true, getIgnoredPrimitiveIndices(*mWorld, settings));

  // Ghost first, so the first click of a click-through cycle lands on it; the
  // rest keep their order, so clicking again still walks what is underneath.
  auto ghost = find(hovered.begin(), hovered.end(), uint32_t(ED_GHOST_INDEX));

  if (ghost != hovered.end()) {
    rotate(hovered.begin(), ghost, ghost + 1);
  }

  return hovered;
}

vector<uint32_t> Document::getPrimitiveIndicesInBounds(wp::BoundingBox const& worldBounds, Settings const& settings) const {
  if (!isActive()) {
    return {};
  }

  auto ignores = getIgnoredPrimitiveIndices(*mWorld, settings);

  vector<uint32_t> result;
  auto numPrimitives = mWorld->getNumPrimitives();

  for (uint32_t index = 0; index < numPrimitives; ++index) {
    if (ignores.find(index) != ignores.end()) {
      continue;
    }

    auto primitive = mWorld->getPrimitive(index);

    if (worldBounds.intersectsBoundingObject(&primitive->getBounds())) {
      result.push_back(index);
    }
  }

  return result;
}

vector<uint32_t> Document::getSelectablePrimitiveIndices(Settings const& settings) const {
  if (!isActive()) {
    return {};
  }

  auto ignores = getIgnoredPrimitiveIndices(*mWorld, settings);

  vector<uint32_t> result;
  auto numPrimitives = mWorld->getNumPrimitives();

  for (uint32_t index = 0; index < numPrimitives; ++index) {
    if (ignores.find(index) == ignores.end()) {
      result.push_back(index);
    }
  }

  return result;
}

uint32_t Document::getHoveredTriggerLineIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  return isActive() && settings.mode != Settings::Mode::Mesh
             ? mWorld->findTriggerLineIndex(
                   mouseWorldPos, settings.triggerLineSelectionDistance,
                   settings.triggerLineHandleRadius)
             : ~0u;
}

bool Document::indexInSelection(uint32_t index) const {
  return mSelectedPrimitiveIndices.find(index) != mSelectedPrimitiveIndices.end();
}

set<uint32_t> const& Document::getSelectedPrimitiveIndices() const {
  return mSelectedPrimitiveIndices;
}

bool Document::anyPrimitiveIndicesSelected(vector<uint32_t> const& indices) const {
  for (auto index : indices) {
    if (find(mSelectedPrimitiveIndices.begin(), mSelectedPrimitiveIndices.end(), index) != mSelectedPrimitiveIndices.end()) {
      return true;
    }
  }

  return false;
}

uint32_t Document::getSelectedWorldVertexIndex() const {
  return mSelectedWorldVertexIndex;
}

uint32_t Document::getSelectedTriggerLineIndex() const {
  return mSelectedTriggerLineIndex;
}

bool Document::hasSelection() const {
  return !mSelectedPrimitiveIndices.empty() || mSelectedTriggerLineIndex != ~0u || mSelectedWorldVertexIndex != ~0u;
}

void Document::setPlayerProxyPosition(wp::Vector2 const& pos) {
  mPlayerOldProxyPosition = mPlayerProxyPosition;
  mPlayerProxyPosition = pos;
}

wp::Vector2 const& Document::getPlayerProxyPosition() const {
  return mPlayerProxyPosition;
}

wp::Vector2 const& Document::getPlayerOldProxyPosition() const {
  return mPlayerOldProxyPosition;
}

void Document::setPlayerProxyAngle(float angle) {
  mPlayerOldProxyAngle = mPlayerProxyAngle;
  mPlayerProxyAngle = angle;
}

float Document::getPlayerProxyAngle() const {
  return mPlayerProxyAngle;
}

float Document::getPlayerOldProxyAngle() const {
  return mPlayerOldProxyAngle;
}

bw::core::Primitive* Document::getGhost() {
  if (isActive()) {
    return mWorld->getPrimitive(0);
  } else {
    throw EditorException("Document not active");
  }
}

void Document::updateGhost(std::shared_ptr<bw::core::World> world, bw::core::Primitive* primitive) {
  primitive->setFlags(primitive->getFlags() | BW_PRIMITIVE_GHOST_FLAG);

  if (world->getNumPrimitives() == 0) {
    world->addPrimitive(primitive);
  } else {
    world->replacePrimitive(0, primitive);
  }
}

std::shared_ptr<bw::core::World> Document::createWorld(float size, float gridSize) {
  auto world = make_shared<bw::core::World>(size, gridSize);

  auto generator = new bw::core::DynamicWorldDataGenerator(world.get());
  generator->setAlwaysUpdateVertices(true);
  generator->setAllowCommitIfVisible(true);
  generator->setPrimitiveFilter(mPrimitiveFilter);
  world->setWorldDataGenerator(generator);

  // Create ghost primitive as a preview for creating primitives
  auto ghost = new bw::core::RegularPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      3);

  ghost->setPriority(0);
  ghost->setPosition(wp::Vector2::ZERO);

  {
    auto mutation = ghost->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::Scale).setPoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::Angle).setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::OrbitAngle).setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::OrbitDistance).setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
  }

  updateGhost(world, ghost);

  return world;
}

void Document::newDoc() {
  reset();

  mWorld = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);
  mModified = false;
}

void Document::closeDoc() {
  reset();
}

bool Document::openDoc(string const& filepath) {
  reset();

  mFilepath = filepath;

  auto path = filesystem::path(mFilepath);
  auto ext = path.extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".yaml" || ext == ".world") {
    shared_ptr<bw::core::Serializer> ser = ext == ".yaml"
                                               ? shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromFile(mFilepath))
                                               : shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromFile(mFilepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      gLogger->error(e.what());
      return false;
    }

    mWorld = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);

    auto workData = bw::core::SerializationWorkData{};

    if (mWorld->deserialize(ser, workData)) {
      auto const& warnings = mWorld->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          gLogger->warn(warning);
        }
      }

      // Add the ghost back into the grids after they have been recreated
      mWorld->replacePrimitive(ED_GHOST_INDEX, mWorld->getPrimitive(ED_GHOST_INDEX), false);
      return true;
    } else {
      auto const& errors = mWorld->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          gLogger->error(error);
        }
      }

      return false;
    }
  } else {
    throw EditorException(format("Could not open {} (filetype not supported)", mFilepath));
  }
}

void Document::saveDoc() {
  if (mFilepath == "") {
    throw EditorException("Document has no filepath set.");
  }

  auto path = std::filesystem::path(mFilepath);
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".yaml" || ext == ".world") {
    shared_ptr<bw::core::Serializer> ser = ext == ".yaml"
                                               ? shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toFile(mFilepath))
                                               : shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::toFile(mFilepath));
    auto workData = bw::core::SerializationWorkData{};

    mWorld->serialize(ser, workData);
    ser->serialize();
  } else {
    throw EditorException(format("Could not save {} (filetype not supported)", mFilepath));
  }

  mModified = false;
}

void Document::saveDocAs(string const& filepath) {
  mFilepath = filepath;
  saveDoc();
}

namespace {
// ".layer.yaml" is a distinct extension from ".yaml" - own weight, own
// dispatch here - not filesystem::path::extension(), which only ever sees
// the last dot-segment and would report ".yaml" for both.
bool hasExtension(string const& filepath, string const& extension) {
  if (filepath.size() < extension.size()) {
    return false;
  }

  auto const tail = filepath.substr(filepath.size() - extension.size());
  return equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
    return tolower(static_cast<unsigned char>(a)) == tolower(static_cast<unsigned char>(b));
  });
}
}  // namespace

void Document::exportLayer(bw::core::Layer const* layer, string const& filepath) const {
  shared_ptr<bw::core::Serializer> ser;

  if (hasExtension(filepath, ".layer.yaml")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toFile(filepath));
  } else if (hasExtension(filepath, ".layer")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::toFile(filepath));
  } else {
    throw EditorException(format("Could not export {} (filetype not supported)", filepath));
  }

  auto workData = bw::core::SerializationWorkData{};
  layer->serialize(ser, workData);
  ser->serialize();
}

bw::core::Layer* Document::importLayer(string const& filepath) {
  shared_ptr<bw::core::Serializer> ser;

  if (hasExtension(filepath, ".layer.yaml")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromFile(filepath));
  } else if (hasExtension(filepath, ".layer")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromFile(filepath));
  } else {
    throw EditorException(format("Could not import {} (filetype not supported)", filepath));
  }

  try {
    ser->deserialize();
  } catch (exception& e) {
    gLogger->error(e.what());
    return nullptr;
  }

  auto layer = make_unique<bw::core::Layer>();

  auto workData = bw::core::SerializationWorkData{};
  workData.accelGridSize = mWorld->getPrimitiveAccelerationGridSize();

  if (!layer->deserialize(ser, workData)) {
    for (auto const& error : layer->getDeserializationErrors()) {
      gLogger->error(error);
    }
    return nullptr;
  }

  for (auto const& warning : layer->getDeserializationWarnings()) {
    gLogger->warn(warning);
  }

  return mWorld->addLayer(layer.release());
}

}  // namespace editor