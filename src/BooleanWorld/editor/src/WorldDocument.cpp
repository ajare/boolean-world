#include "WorldDocument.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <format>
#include <utility>

#include <spdlog/spdlog.h>

#include "common/GameDefines.h"
#include "core/BinarySerializer.h"
#include "core/DynamicWorldDataGenerator.h"
#include "core/RegularPolygon.h"
#include "core/YamlSerializer.h"
#include "Defines.h"
#include "EditorException.h"
#include "Undo.h"

extern spdlog::logger* gLogger;

namespace editor {
using namespace std;

namespace {
bool hasExtension(string const& filepath, string const& extension) {
  if (filepath.size() < extension.size()) return false;
  auto const tail = filepath.substr(filepath.size() - extension.size());
  return equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
    return tolower(static_cast<unsigned char>(a)) ==
           tolower(static_cast<unsigned char>(b));
  });
}

bw::core::Primitive* createEditorGhost() {
  auto* ghost = new bw::core::RegularPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 3);
  ghost->setPriority(0);
  ghost->setPosition(wp::Vector2::ZERO);
  ghost->setFlags(ghost->getFlags() | BW_PRIMITIVE_GHOST_FLAG);
  {
    auto mutation = ghost->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::Scale)
        .setPoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::Angle)
        .setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::OrbitAngle)
        .setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::OrbitDistance)
        .setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
  }
  return ghost;
}
}  // namespace

void WorldDocument::clearTransientState() {}

void WorldDocument::resetWorldDocument() {
  clearUndoHistory();
  mModified = false;
  mFilepath.clear();
  mWorld.reset();
  clearTransientState();
}

bool WorldDocument::isActive() const {
  return mWorld != nullptr;
}

void WorldDocument::setModified(bool modified) {
  mModified = modified;
}

bool WorldDocument::isModified() const {
  return mModified;
}

string const& WorldDocument::getFilepath() const {
  return mFilepath;
}

bool WorldDocument::hasFilepath() const {
  return mFilepath != "";
}

void WorldDocument::setWorld(bw::core::World const& world) {
  mWorld = make_shared<bw::core::World>(world);
}

WorldSnapshot WorldDocument::captureWorldSnapshot() const {
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
    snapshot.generationStartInterval = dynamicGenerator->getGenerationStartInterval();
  }

  return snapshot;
}

void WorldDocument::restoreWorldSnapshot(WorldSnapshot const& snapshot) {
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
    dynamicGenerator->setGenerationStartInterval(snapshot.generationStartInterval);
  }

  if (mWorldDependencyLoader) {
    string error;
    if (!mWorldDependencyLoader(world->getDependentResourceNames(), &error)) {
      throw EditorException("Could not restore World dependencies: " + error);
    }
  }
  mWorld = move(world);
}

void WorldDocument::setPrimitiveFilter(bw::core::PrimitiveFilter filter) {
  mPrimitiveFilter = move(filter);

  if (mWorld) {
    mWorld->getWorldDataGenerator()->setPrimitiveFilter(mPrimitiveFilter);
  }
}

shared_ptr<bw::core::World> WorldDocument::getWorld() {
  return mWorld;
}

shared_ptr<bw::core::World const> WorldDocument::getWorld() const {
  return mWorld;
}

bw::core::Primitive* WorldDocument::getGhost() {
  if (isActive()) {
    return mWorld->getPrimitive(0);
  } else {
    throw EditorException("Document not active");
  }
}

void WorldDocument::updateGhost(std::shared_ptr<bw::core::World> world, bw::core::Primitive* primitive) {
  primitive->setFlags(primitive->getFlags() | BW_PRIMITIVE_GHOST_FLAG);

  if (world->getNumPrimitives() == 0) {
    world->addPrimitive(primitive);
  } else {
    world->replacePrimitive(0, primitive);
  }
}

std::shared_ptr<bw::core::World> WorldDocument::createWorld(float size, float gridSize) {
  auto world = make_shared<bw::core::World>(size, gridSize);

  auto generator = new bw::core::DynamicWorldDataGenerator(world.get(), false);
  generator->setAlwaysUpdateVertices(true);
  generator->setAllowCommitIfVisible(true);
  generator->setPrimitiveFilter(mPrimitiveFilter);
  world->setWorldDataGenerator(generator);

  // Create ghost primitive as a preview for creating primitives.
  updateGhost(world, createEditorGhost());

  return world;
}

void WorldDocument::newDoc() {
  resetWorldDocument();
  if (mWorldDependencyLoader) {
    string ignored;
    mWorldDependencyLoader({}, &ignored);
  }

  mWorld = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);
  mModified = false;
}

void WorldDocument::closeDoc() {
  resetWorldDocument();
  if (mWorldDependencyLoader) {
    string ignored;
    mWorldDependencyLoader({}, &ignored);
  }
}

bool WorldDocument::beginOpenDoc(string const& filepath) {
  if (mPendingWorldOpen) {
    mAsyncWorldOpenError = "A World is already loading.";
    return false;
  }

  auto const yaml = hasExtension(filepath, ".world.yaml");
  auto const binary = hasExtension(filepath, ".world") && !yaml;
  if (!yaml && !binary) {
    mAsyncWorldOpenError = format(
        "Could not open {} (filetype not supported)", filepath);
    return false;
  }

  auto previousDependencies =
      mWorld ? mWorld->getDependentResourceNames() : vector<string>{};
  try {
    auto dependencyReader = yaml
                                ? shared_ptr<bw::core::Serializer>(
                                      bw::core::YamlSerializer::fromFile(filepath))
                                : shared_ptr<bw::core::Serializer>(
                                      bw::core::BinarySerializer::fromFile(filepath));
    dependencyReader->deserialize();
    if (mWorldDependencyLoader) {
      string error;
      if (!mWorldDependencyLoader(
              bw::core::World::readDependentResourceNames(dependencyReader),
              &error)) {
        mAsyncWorldOpenError = move(error);
        return false;
      }
    }
  } catch (exception const& error) {
    mAsyncWorldOpenError = error.what();
    return false;
  }

  auto candidate = createWorld(
      ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);
  auto progress = make_shared<AsyncWorldOpenProgress>();
  progress->part = "Deserializing World";
  try {
    auto result = async(
        launch::async,
        [filepath, yaml, candidate, progress]() -> AsyncWorldOpenResult {
          AsyncWorldOpenResult outcome;
          auto reportProgress = [progress](string const& part) {
            scoped_lock lock(progress->mutex);
            progress->part = part;
          };
          try {
            auto serializer = yaml
                                  ? shared_ptr<bw::core::Serializer>(
                                        bw::core::YamlSerializer::fromFile(filepath))
                                  : shared_ptr<bw::core::Serializer>(
                                        bw::core::BinarySerializer::fromFile(filepath));
            serializer->deserialize();
            auto workData = bw::core::SerializationWorkData{};
            workData.allowEmptyWorld = true;
            workData.preserveTargetGhostPrimitive = true;
            workData.progress = reportProgress;
            if (candidate->deserialize(serializer, workData)) {
              // The editor's first getWorldData() performs a blocking initial
              // generation. Do it before completing the future so the loading
              // modal remains visible and animated for the entire operation.
              reportProgress("Generating initial WorldData");
              (void)candidate->getWorldData();
              outcome.world = candidate;
              outcome.warnings = candidate->getDeserializationWarnings();
            } else {
              outcome.errors = candidate->getDeserializationErrors();
            }
          } catch (exception const& error) {
            outcome.errors.push_back(error.what());
          }
          return outcome;
        });
    mPendingWorldOpen.emplace(PendingWorldOpen{
        filepath, move(previousDependencies), progress, move(result)});
    mAsyncWorldOpenError.clear();
    return true;
  } catch (exception const& error) {
    if (mWorldDependencyLoader) {
      string ignored;
      mWorldDependencyLoader(previousDependencies, &ignored);
    }
    mAsyncWorldOpenError = error.what();
    return false;
  }
}

bool WorldDocument::isOpeningWorld() const {
  return mPendingWorldOpen.has_value();
}

AsyncWorldOpenStatus WorldDocument::pollOpenDoc() {
  if (!mPendingWorldOpen) return AsyncWorldOpenStatus::Idle;
  if (mPendingWorldOpen->result.wait_for(chrono::seconds(0)) !=
      future_status::ready) {
    return AsyncWorldOpenStatus::Loading;
  }

  auto filepath = move(mPendingWorldOpen->filepath);
  auto previousDependencies =
      move(mPendingWorldOpen->previousDependencies);
  AsyncWorldOpenResult result;
  try {
    result = mPendingWorldOpen->result.get();
  } catch (exception const& error) {
    result.errors.push_back(error.what());
  }
  mPendingWorldOpen.reset();

  if (!result.world) {
    if (mWorldDependencyLoader) {
      string ignored;
      mWorldDependencyLoader(previousDependencies, &ignored);
    }
    for (auto const& error : result.errors) gLogger->error(error);
    mAsyncWorldOpenError = result.errors.empty()
                               ? "Could not load World."
                               : result.errors.front();
    return AsyncWorldOpenStatus::Failed;
  }

  for (auto const& warning : result.warnings) gLogger->warn(warning);
  resetWorldDocument();
  mFilepath = move(filepath);
  mWorld = move(result.world);
  mAsyncWorldOpenError.clear();
  return AsyncWorldOpenStatus::Succeeded;
}

string WorldDocument::getAsyncWorldOpenProgress() const {
  if (!mPendingWorldOpen) return {};
  auto const& progress = mPendingWorldOpen->progress;
  scoped_lock lock(progress->mutex);
  return progress->part;
}

string const& WorldDocument::getAsyncWorldOpenError() const {
  return mAsyncWorldOpenError;
}

void WorldDocument::setWorldDependencyLoader(
    function<bool(vector<string> const&, string*)> loader) {
  mWorldDependencyLoader = move(loader);
}

bool WorldDocument::openDoc(string const& filepath) {
  auto const yaml = hasExtension(filepath, ".world.yaml");
  auto const binary = hasExtension(filepath, ".world") && !yaml;

  if (yaml || binary) {
    shared_ptr<bw::core::Serializer> ser = yaml
                                               ? shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromFile(filepath))
                                               : shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromFile(filepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      gLogger->error(e.what());
      return false;
    }

    auto previousDependencies = mWorld
                                    ? mWorld->getDependentResourceNames()
                                    : vector<string>{};
    if (mWorldDependencyLoader) {
      try {
        auto dependencyReader = yaml
                                    ? shared_ptr<bw::core::Serializer>(
                                          bw::core::YamlSerializer::fromFile(filepath))
                                    : shared_ptr<bw::core::Serializer>(
                                          bw::core::BinarySerializer::fromFile(filepath));
        dependencyReader->deserialize();
        string error;
        if (!mWorldDependencyLoader(
                bw::core::World::readDependentResourceNames(dependencyReader),
                &error)) {
          gLogger->error(error);
          return false;
        }
      } catch (exception const& error) {
        gLogger->error(error.what());
        return false;
      }
    }

    auto restoreDependencies = [&] {
      if (mWorldDependencyLoader) {
        string ignored;
        mWorldDependencyLoader(previousDependencies, &ignored);
      }
    };

    auto candidate = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);

    auto workData = bw::core::SerializationWorkData{};
    workData.allowEmptyWorld = true;
    workData.preserveTargetGhostPrimitive = true;

    if (candidate->deserialize(ser, workData)) {
      auto const& warnings = candidate->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          gLogger->warn(warning);
        }
      }

      resetWorldDocument();
      mFilepath = filepath;
      mWorld = move(candidate);
      return true;
    } else {
      auto const& errors = candidate->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          gLogger->error(error);
        }
      }

      restoreDependencies();
      return false;
    }
  } else {
    throw EditorException(format("Could not open {} (filetype not supported)", filepath));
  }
}

void WorldDocument::saveDoc() {
  if (mFilepath == "") {
    throw EditorException("Document has no filepath set.");
  }

  auto const yaml = hasExtension(mFilepath, ".world.yaml");
  auto const binary = hasExtension(mFilepath, ".world") && !yaml;

  if (yaml || binary) {
    shared_ptr<bw::core::Serializer> ser = yaml
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

void WorldDocument::saveDocAs(string const& filepath) {
  auto const yaml = hasExtension(filepath, ".world.yaml");
  auto const binary = hasExtension(filepath, ".world") && !yaml;
  if (!yaml && !binary) {
    throw EditorException(format("Could not save {} (filetype not supported)", filepath));
  }

  mFilepath = filepath;
  saveDoc();
}

void WorldDocument::exportLayer(bw::core::Layer const* layer, string const& filepath) const {
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

bw::core::Layer* WorldDocument::importLayer(string const& filepath) {
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
