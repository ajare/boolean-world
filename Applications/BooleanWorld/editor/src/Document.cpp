#include <exception>
#include <filesystem>
#include <fstream>
#include <format>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <yaml-cpp/yaml.h>

#include "core/YamlSerializer.h"
#include "core/RegularPolygon.h"
#include "core/DynamicWorldDataGenerator.h"
#include "core/Vertex.h"
#include "core/MeshPrimitive.h"

#include "common/GameDefines.h"

#include "Defines.h"
#include "Document.h"
#include "EditorException.h"
#include "AppHelpers.h"
#include "Tiled.h"

extern spdlog::logger* gLogger;

namespace editor {
using namespace std;

Document* Document::msInstance = nullptr;

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
  set_union(
      mSelectedPrimitiveIndices.begin(),
      mSelectedPrimitiveIndices.end(),
      indices.begin(),
      indices.end(),
      inserter(mSelectedPrimitiveIndices, mSelectedPrimitiveIndices.begin()));
}

void Document::removeSelectedPrimitiveIndex(uint32_t index) {
  mSelectedPrimitiveIndices.erase(index);
}

void Document::removeSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  set_difference(
      mSelectedPrimitiveIndices.begin(),
      mSelectedPrimitiveIndices.end(),
      indices.begin(),
      indices.end(),
      inserter(mSelectedPrimitiveIndices, mSelectedPrimitiveIndices.begin()));
}

void Document::clearSelections() {
  mSelectedPrimitiveIndices.clear();
  mSelectedWorldVertexIndex = ~0u;
  mSelectedTriggerLineIndex = ~0u;
}

uint32_t Document::getHoveredPrimitiveIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  set<uint32_t> ignores;

  if (!settings.ghostActive) {
    ignores.insert(0);
  }

  if (!settings.renderAnimatedPrimitives) {
    for (uint32_t i = 0; i < mWorld->getNumPrimitives(); ++i) {
      if (!mWorld->getPrimitive(i)->isStatic()) {
        ignores.insert(i);
      }
    }
  }

  return isActive() ? mWorld->findPrimitiveIndex(mouseWorldPos, true, ignores) : ~0u;
}

vector<uint32_t> Document::getHoveredPrimitiveIndices(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  set<uint32_t> ignores;

  if (!settings.ghostActive) {
    ignores.insert(0);
  }

  if (!settings.renderAnimatedPrimitives) {
    for (uint32_t i = 0; i < mWorld->getNumPrimitives(); ++i) {
      if (!mWorld->getPrimitive(i)->isStatic()) {
        ignores.insert(i);
      }
    }
  }

  return isActive() ? mWorld->findPrimitiveIndices(mouseWorldPos, true, ignores) : vector<uint32_t>();
}

uint32_t Document::getHoveredTriggerLineIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  return isActive() ? mWorld->findTriggerLineIndex(mouseWorldPos, settings.triggerLineSelectionDistance, settings.triggerLineHandleRadius) : ~0u;
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

  auto genFn = [world](wp::Vector2 offset, int dimX, int dimY, float cellSize) {
    auto wdg = new bw::core::DynamicWorldDataGenerator(world.get());

    wdg->setBroadPhaseCulling(bw::core::WorldDataGenerator::BroadPhaseCulling::Circle);
    wdg->setNarrowPhaseCulling(bw::core::WorldDataGenerator::NarrowPhaseCulling::None);

    wdg->setAlwaysUpdateVertices(true);
    wdg->setAllowCommitIfVisible(true);

    return wdg;
  };

  world->setWorldDataGeneratorFactory(genFn);

  // Create ghost primitive as a preview for creating primitives
  auto ghost = new bw::core::RegularPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      3);

  ghost->setPriority(0);
  ghost->setPosition(wp::Vector2::ZERO);

  ghost->setAnimationValues(bw::core::VertexTransformer::Key::Scale, {{0.0f, 1.0f}, {1.0f, 1.0f}});
  ghost->setAnimationValues(bw::core::VertexTransformer::Key::Angle, {{0.0f, 0.0f}, {1.0f, 0.0f}});
  ghost->setAnimationValues(bw::core::VertexTransformer::Key::OrbitAngle, {{0.0f, 0.0f}, {1.0f, 0.0f}});
  ghost->setAnimationValues(bw::core::VertexTransformer::Key::OrbitDistance, {{0.0f, 0.0f}, {1.0f, 0.0f}});

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

  if (ext == ".yaml") {
    auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::fromFile(mFilepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      gLogger->error(e.what());
      return false;
    }

    if (mWorld) {
      throw EditorException("Tried to create a new document with an existing one.");
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
  } else if (ext == ".json") {
    mWorld = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);

    loadTiledPrefabFile(mFilepath, mWorld);
    return true;
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

  if (ext == ".yaml") {
    auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::toFile(mFilepath));
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

void Document::loadTiledPrefabFile(string const& filepath, shared_ptr<bw::core::World> world) {
  ::openTiledPrefabFile(filepath, world);
}

void Document::addPrefabInstance(bw::core::World const* prefab, int tileX, int tileY, float rotation, uint8_t layer) {
  float prefabScale = BW_PLAYER_RADIUS * BW_PREFAB_PLAYER_RATIO;
  wp::Vector2 offset = {(tileX + 0.5f) * prefabScale, (tileY + 0.5f) * prefabScale};

  // Copy all trigger lines first, updating their index for the Primitives to use
  auto prefabTriggerLines = prefab->getTriggerLines();
  map<uint32_t, uint32_t> triggerLineMap;

  for (auto prefabTriggerLine : prefabTriggerLines) {
    auto triggerLineCopy = new bw::core::WorldTriggerLine(*prefabTriggerLine);
    auto tlc = triggerLineCopy->getPoint(0).lerp(triggerLineCopy->getPoint(1), 0.5f);

    for (uint32_t i = 0; i < 2; ++i) {
      auto v = triggerLineCopy->getPoint(i);

      v -= tlc;

      v.rotateAnticlockwise(rotation);

      v += tlc.rotatedAnticlockwiseCopy(rotation);
      v += offset;

      // Move points
      triggerLineCopy->setPoint(i, v);
    }

    mWorld->addTriggerLine(triggerLineCopy);

    triggerLineMap[prefabTriggerLine->getId()] = triggerLineCopy->getId();
  }

  // Get all Primitives from prefab
  auto prefabPrims = prefab->getPrimitives();

  // Copy each one, translating it
  for (auto prefabPrim : prefabPrims) {
    auto primCopy = prefabPrim->copy();

    // Rotate position
    auto newPos = primCopy->getPosition().rotatedClockwiseCopy(rotation);

    primCopy->setPosition(newPos + offset);
    primCopy->setOrientation(rotation);
    primCopy->setLayer(layer);

    // Update transform offset
    primCopy->setTransformOffset(prefabPrim->getTransformOffset().rotatedClockwiseCopy(rotation));

    // Rotate eye offset
    primCopy->setInfluenceEyeOriginOffset(prefabPrim->getInfluenceEyeOriginOffset().rotatedClockwiseCopy(rotation));

    // Update trigger line refs
    primCopy->updateTransformTriggerLineIndices(triggerLineMap);

    mWorld->addPrimitive(primCopy);
  }
}
}  // namespace editor