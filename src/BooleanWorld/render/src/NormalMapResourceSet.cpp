#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <GL/glew.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

#include <mpp/ProgrammaticTextureStream.h>
#include <mpp/ResourceManager.h>
#include <mpp/TextureParams.h>

#include "NormalMapResourceSet.h"

namespace {
constexpr uint32_t kMaximumDimension = 8192;
constexpr uint64_t kMaximumPixels = uint64_t{kMaximumDimension} * kMaximumDimension;
constexpr uintmax_t kMaximumEncodedBytes = 256U * 1024U * 1024U;

[[noreturn]] void fail(std::filesystem::path const& path, std::string const& reason) {
  throw std::runtime_error("Normal map '" + path.generic_string() + "': " + reason);
}

bool isBelow(std::filesystem::path const& path, std::filesystem::path const& root) {
  auto pathPart = path.begin();
  for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++pathPart) {
    if (pathPart == path.end() || *pathPart != *rootPart) return false;
  }
  return true;
}
}  // namespace

std::filesystem::path const& NormalMapImage::path() const noexcept { return mPath; }
uint32_t NormalMapImage::width() const noexcept { return mWidth; }
uint32_t NormalMapImage::height() const noexcept { return mHeight; }
uint32_t NormalMapImage::channels() const noexcept { return mChannels; }
mpp::ResourcePtr const& NormalMapImage::texture() const noexcept { return mTexture; }

NormalMapResourceSet::NormalMapResourceSet(
    std::filesystem::path resourceRoot, mpp::ResourceManager& resourceManager)
    : mResourceManager(&resourceManager) {
  std::error_code error;
  mResourceRoot = std::filesystem::canonical(resourceRoot, error);
  if (error || !std::filesystem::is_directory(mResourceRoot, error)) {
    throw std::runtime_error("Normal-map resource root '" + resourceRoot.generic_string() +
                             "' is not a readable directory.");
  }
}

NormalMapResourceSet::~NormalMapResourceSet() {
  for (auto& [path, image] : mImages) destroyTexture(*image);
}

std::filesystem::path NormalMapResourceSet::normalize(
    std::filesystem::path const& reference) const {
  if (reference.empty()) fail(reference, "path is empty.");
  if (reference.is_absolute() || reference.has_root_name()) {
    fail(reference, "path must be relative to the application resource root.");
  }

  std::error_code error;
  auto resolved = std::filesystem::weakly_canonical(
      mResourceRoot / reference.lexically_normal(), error);
  if (error) fail(reference, "could not canonicalize path: " + error.message());
  if (!isBelow(resolved, mResourceRoot)) {
    fail(reference, "path escapes the application resource root.");
  }
  return resolved.lexically_relative(mResourceRoot);
}

void NormalMapResourceSet::decode(NormalMapImage& image) {
  auto absolutePath = mResourceRoot / image.mPath;
  std::error_code error;
  if (!std::filesystem::is_regular_file(absolutePath, error)) {
    fail(image.mPath, error ? "file cannot be read: " + error.message() : "file does not exist or is not a regular file.");
  }
  auto encodedSize = std::filesystem::file_size(absolutePath, error);
  if (error) fail(image.mPath, "file size cannot be read: " + error.message());
  if (encodedSize == 0) fail(image.mPath, "file is empty.");
  if (encodedSize > kMaximumEncodedBytes ||
      encodedSize > static_cast<uintmax_t>(std::numeric_limits<int>::max())) {
    fail(image.mPath, "encoded file is too large.");
  }

  std::ifstream stream(absolutePath, std::ios::binary);
  if (!stream) fail(image.mPath, "file cannot be opened for reading.");
  std::vector<uint8_t> encoded(static_cast<size_t>(encodedSize));
  stream.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
  if (!stream) fail(image.mPath, "file cannot be read completely.");

  int width = 0, height = 0, channels = 0;
  if (!stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                             &width, &height, &channels)) {
    fail(image.mPath, "file is not a decoder-supported image.");
  }
  if (stbi_is_16_bit_from_memory(encoded.data(), static_cast<int>(encoded.size()))) {
    fail(image.mPath, "image must use 8-bit RGB or RGBA samples.");
  }
  if (width <= 0 || height <= 0) fail(image.mPath, "image has zero dimensions.");
  if (channels != 3 && channels != 4) {
    fail(image.mPath, "image must be RGB or RGBA; grayscale images are not normal maps.");
  }
  if (width > static_cast<int>(kMaximumDimension) ||
      height > static_cast<int>(kMaximumDimension) ||
      uint64_t(width) * uint64_t(height) > kMaximumPixels) {
    fail(image.mPath, "image exceeds the maximum size of 8192 by 8192 pixels.");
  }

  stbi_uc* decoded = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                           &width, &height, &channels, 0);
  if (!decoded) fail(image.mPath, "image decoding failed.");
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(decoded, stbi_image_free);
  auto sourceRowBytes = size_t(width) * size_t(channels);
  auto rowBytes = size_t(width) * 3;
  image.mPixels.resize(rowBytes * size_t(height));
  // MPP's texture path, like ImageResource, uses a bottom-to-top pixel layout.
  // Normal maps are vectors, so an RGBA source's alpha is intentionally not
  // retained in the uploaded data.
  for (int y = 0; y < height; ++y) {
    auto const* source = pixels.get() + size_t(height - y - 1) * sourceRowBytes;
    auto* destination = image.mPixels.data() + size_t(y) * rowBytes;
    for (int x = 0; x < width; ++x) {
      std::copy_n(source + size_t(x) * channels, 3, destination + size_t(x) * 3);
    }
  }
  image.mWidth = static_cast<uint32_t>(width);
  image.mHeight = static_cast<uint32_t>(height);
  image.mChannels = 3;
}

void NormalMapResourceSet::createTexture(NormalMapImage& image) {
  auto stream = new mpp::ProgrammaticTextureStream(mResourceManager);
  stream->setTarget(mpp::TextureTarget::Texture2D);
  stream->setColourSpace(mpp::TextureColourSpace::Linear);
  stream->setWrapping(mpp::TextureParams::Wrapping::Repeat);
  stream->setFiltering(mpp::TextureParams::MinFilter::LinearMipmapLinear,
                       mpp::TextureParams::MagFilter::Linear);
  stream->enableMipMaps(true);
  stream->setData([&image](std::string const&) {
    mpp::TextureData data;
    data.width = image.mWidth;
    data.height = image.mHeight;
    data.bitsPerPixel = image.mChannels * 8;
    data.dataType = GL_UNSIGNED_BYTE;
    data.pixelFormat = image.mChannels == 3 ? GL_RGB : GL_RGBA;
    data.data = new uint8_t[image.mPixels.size()];
    std::copy(image.mPixels.begin(), image.mPixels.end(), data.data);
    return data;
  });

  image.mTextureName = "BooleanWorld.NormalMap." + std::to_string(++mNextTextureId);
  image.mTexture = mResourceManager->declareResource(
                                       image.mTextureName, mpp::ResourceStreamPtr(stream))
                       .first;
  image.mTexture->create();
  image.mTexture->load();
}

void NormalMapResourceSet::destroyTexture(NormalMapImage& image) noexcept {
  if (!image.mTexture) return;
  try {
    mResourceManager->deleteResource(image.mTextureName);
  } catch (...) {
    // Destruction is best effort only: MPP reports a referenced texture through
    // its logger, but no resource in this boundary acquires it externally.
  }
  image.mTexture.reset();
  image.mTextureName.clear();
}

std::shared_ptr<NormalMapImage const> NormalMapResourceSet::acquire(
    std::filesystem::path const& resourceRelativePath) {
  auto key = normalize(resourceRelativePath);
  if (auto found = mImages.find(key); found != mImages.end()) return found->second;

  auto image = std::make_shared<NormalMapImage>();
  image->mPath = key;
  decode(*image);
  try {
    createTexture(*image);
  } catch (...) {
    destroyTexture(*image);
    throw;
  }
  mImages.emplace(key, image);
  return image;
}

void NormalMapResourceSet::reload(std::filesystem::path const& resourceRelativePath) {
  auto key = normalize(resourceRelativePath);
  auto found = mImages.find(key);
  if (found == mImages.end()) {
    (void)acquire(key);
    return;
  }

  NormalMapImage replacement;
  replacement.mPath = key;
  decode(replacement);
  createTexture(replacement);
  destroyTexture(*found->second);
  found->second->mWidth = replacement.mWidth;
  found->second->mHeight = replacement.mHeight;
  found->second->mChannels = replacement.mChannels;
  found->second->mPixels = std::move(replacement.mPixels);
  found->second->mTexture = std::move(replacement.mTexture);
  found->second->mTextureName = std::move(replacement.mTextureName);
}

size_t NormalMapResourceSet::size() const noexcept { return mImages.size(); }
