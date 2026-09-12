#pragma once

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/World.h"

namespace editor {

struct WorldSnapshot {
  std::string serializedWorld;
  float accelerationGridSize{-1.0f};
  bw::core::LayerSelection layerSelection;
  bool alwaysUpdateWorldVertices{false};
  bool hasDynamicGenerator{false};
  bool alwaysUpdateGeneratorVertices{false};
  bool allowCommitIfVisible{false};
  float generationStartInterval{5.0f};
};

// Owns the persistent document state and all World/file serialization. Editor
// interaction state belongs to Document and Selection instead.
enum class AsyncWorldOpenStatus { Idle,
                                  Loading,
                                  Succeeded,
                                  Failed };

class WorldDocument {
  struct AsyncWorldOpenResult {
    std::shared_ptr<bw::core::World> world;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
  };

  struct AsyncWorldOpenProgress {
    mutable std::mutex mutex;
    std::string part;
  };

  struct PendingWorldOpen {
    std::string filepath;
    std::vector<std::string> previousDependencies;
    std::shared_ptr<AsyncWorldOpenProgress> progress;
    std::future<AsyncWorldOpenResult> result;
  };

protected:
  bool mModified{false};
  std::string mFilepath;
  std::shared_ptr<bw::core::World> mWorld;
  bw::core::PrimitiveFilter mPrimitiveFilter;
  std::function<bool(std::vector<std::string> const&, std::string*)>
      mWorldDependencyLoader;
  std::optional<PendingWorldOpen> mPendingWorldOpen;
  std::string mAsyncWorldOpenError;

  virtual void clearTransientState();
  void resetWorldDocument();
  std::shared_ptr<bw::core::World> createWorld(float size, float gridSize);

public:
  virtual ~WorldDocument() = default;

  [[nodiscard]] bool isActive() const;
  void setModified(bool modified = true);
  [[nodiscard]] bool isModified() const;
  [[nodiscard]] std::string const& getFilepath() const;
  [[nodiscard]] bool hasFilepath() const;
  void setWorld(bw::core::World const& world);
  [[nodiscard]] WorldSnapshot captureWorldSnapshot() const;
  void restoreWorldSnapshot(WorldSnapshot const& snapshot);
  void setPrimitiveFilter(bw::core::PrimitiveFilter filter);
  [[nodiscard]] std::shared_ptr<bw::core::World> getWorld();
  [[nodiscard]] std::shared_ptr<bw::core::World const> getWorld() const;

  bw::core::Primitive* getGhost();
  void updateGhost(std::shared_ptr<bw::core::World> world,
                   bw::core::Primitive* primitive);

  void newDoc();
  void closeDoc();
  bool openDoc(std::string const& filepath);
  bool beginOpenDoc(std::string const& filepath);
  [[nodiscard]] bool isOpeningWorld() const;
  AsyncWorldOpenStatus pollOpenDoc();
  [[nodiscard]] std::string getAsyncWorldOpenProgress() const;
  [[nodiscard]] std::string const& getAsyncWorldOpenError() const;
  void setWorldDependencyLoader(
      std::function<bool(std::vector<std::string> const&, std::string*)> loader);
  void saveDoc();
  void saveDocAs(std::string const& filepath);
  void exportLayer(bw::core::Layer const* layer,
                   std::string const& filepath) const;
  bw::core::Layer* importLayer(std::string const& filepath);
};

}  // namespace editor
