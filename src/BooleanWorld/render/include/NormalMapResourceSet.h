#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <mpp/Resource.h>

namespace mpp {
class ResourceManager;
}

namespace bw::core {
class World;
}

// These checks deliberately decode from disk without creating a GPU resource.
// Authoring and World activation use them before replacing their active state.
void validateNormalMapImage(std::filesystem::path const& resourceRoot,
                            std::filesystem::path const& resourceRelativePath);
void validateWorldNormalMaps(bw::core::World const& world,
                             std::filesystem::path const& resourceRoot);

// A reusable, application-resource-relative normal-map asset.  This is kept
// separate from ImageResource because normal maps are vector data: their
// decoding and sampler contract must not depend on generic image tags.
class NormalMapImage {
public:
  [[nodiscard]] std::filesystem::path const& path() const noexcept;
  [[nodiscard]] uint32_t width() const noexcept;
  [[nodiscard]] uint32_t height() const noexcept;
  [[nodiscard]] uint32_t channels() const noexcept;
  [[nodiscard]] mpp::ResourcePtr const& texture() const noexcept;

public:  // Decoder staging data; only NormalMapResourceSet exposes it to callers.
  std::filesystem::path mPath;
  uint32_t mWidth{};
  uint32_t mHeight{};
  uint32_t mChannels{};
  std::vector<uint8_t> mPixels;
  mpp::ResourcePtr mTexture;
  std::string mTextureName;
};

// Owns all normal maps referenced by one renderer/resource set. References
// are normalized relative to resourceRoot; acquire() never polls the file
// system after the first load, while reload() is the explicit refresh point.
class NormalMapResourceSet {
public:
  NormalMapResourceSet(std::filesystem::path resourceRoot,
                       mpp::ResourceManager& resourceManager);
  ~NormalMapResourceSet();

  NormalMapResourceSet(NormalMapResourceSet const&) = delete;
  NormalMapResourceSet& operator=(NormalMapResourceSet const&) = delete;

  // Re-reads and decodes a candidate without creating a texture or caching it.
  static void validateImage(std::filesystem::path const& resourceRoot,
                            std::filesystem::path const& resourceRelativePath);

  [[nodiscard]] std::shared_ptr<NormalMapImage const> acquire(
      std::filesystem::path const& resourceRelativePath);
  void reload(std::filesystem::path const& resourceRelativePath);

  [[nodiscard]] size_t size() const noexcept;

private:
  std::filesystem::path normalize(std::filesystem::path const& reference) const;
  void decode(NormalMapImage& image);
  void createTexture(NormalMapImage& image);
  void destroyTexture(NormalMapImage& image) noexcept;

  std::filesystem::path mResourceRoot;
  mpp::ResourceManager* mResourceManager;
  std::map<std::filesystem::path, std::shared_ptr<NormalMapImage>> mImages;
  uint64_t mNextTextureId{};
};
