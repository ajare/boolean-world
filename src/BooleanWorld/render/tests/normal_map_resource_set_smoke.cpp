// Manual GPU smoke test for the public normal-map resource boundary. It is
// deliberately not registered with CTest because it needs a real OpenGL driver.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/Texture.h>
#include <mpp/TextureStream.h>

#include "NormalMapResourceSet.h"

namespace {
namespace fs = std::filesystem;

void require(bool value, std::string const& message) {
  if (!value) throw std::runtime_error(message);
}

void writeBytes(fs::path const& path, std::vector<unsigned char> const& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
  require(bool(output), "Could not write " + path.string());
}

void writeRgbPpm(fs::path const& path, unsigned char red) {
  std::string header = "P6\n2 1\n255\n";
  std::vector<unsigned char> data(header.begin(), header.end());
  data.insert(data.end(), {red, 128, 255, red, 128, 255});
  writeBytes(path, data);
}

void writeRgbaTga(fs::path const& path) {
  // Uncompressed 32-bit true-colour TGA; its zero alpha verifies that alpha
  // does not make an otherwise valid vector image unacceptable.
  std::vector<unsigned char> data(18, 0);
  data[2] = 2;
  data[12] = 1;
  data[14] = 1;
  data[16] = 32;
  data.insert(data.end(), {255, 128, 64, 0});  // BGRA
  writeBytes(path, data);
}

void writeGrayscaleAlphaTga(fs::path const& path) {
  std::vector<unsigned char> data(18, 0);
  data[2] = 3;  // Uncompressed grayscale.
  data[12] = 1;
  data[14] = 1;
  data[16] = 16;
  data.insert(data.end(), {128, 0});  // Luminance + alpha.
  writeBytes(path, data);
}

void requireRejected(NormalMapResourceSet& set, fs::path const& path,
                     std::string const& description) {
  try {
    (void)set.acquire(path);
  } catch (std::exception const& error) {
    require(std::string(error.what()).find("Normal map") != std::string::npos,
            description + " did not provide an actionable normal-map error.");
    return;
  }
  throw std::runtime_error(description + " was accepted.");
}
}  // namespace

int main() {
  fs::path root = fs::temp_directory_path() /
                  ("boolean-world-normal-maps-" + std::to_string(
                                                      std::chrono::steady_clock::now().time_since_epoch().count()));
  int result = 1;
  SDL_Window* window = nullptr;
  SDL_GLContext context{};
  try {
    writeRgbPpm(root / "maps" / "normal.ppm", 10);
    writeRgbaTga(root / "maps" / "rgba.tga");
    writeGrayscaleAlphaTga(root / "maps" / "gray-alpha.tga");
    writeBytes(root / "maps" / "gray.pgm",
               std::vector<unsigned char>{'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 0});
    writeBytes(root / "maps" / "invalid.bin", {0, 1, 2});
    writeBytes(root / "maps" / "zero.bin", {});
    std::string huge = "P6\n8193 1\n255\n";
    writeBytes(root / "maps" / "huge.ppm",
               std::vector<unsigned char>(huge.begin(), huge.end()));

    require(SDL_Init(SDL_INIT_VIDEO), "SDL_Init failed");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    window = SDL_CreateWindow("normal-map-resource-set", 16, 16,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    require(window != nullptr, "SDL_CreateWindow failed");
    context = SDL_GL_CreateContext(window);
    require(context != nullptr, "SDL_GL_CreateContext failed");
    SDL_GL_MakeCurrent(window, context);
    glewExperimental = GL_TRUE;
    require(glewInit() == GLEW_OK, "glewInit failed");

    mpp::Logger logger;
    require(logger.initialise("normal-map-resource-set-smoke.log", mpp::Logger::Level::Error),
            "Could not initialize MPP logger");
    mpp::RenderSystem renderSystem(16, 16, &logger);
    mpp::ResourceManager resources(&renderSystem, &logger);

    std::string textureName;
    {
      NormalMapResourceSet set(root, resources);
      auto rgb = set.acquire("maps/./normal.ppm");
      auto same = set.acquire("maps\\normal.ppm");
      require(rgb == same && set.size() == 1, "Equivalent paths did not share one cached image.");
      require(rgb->width() == 2 && rgb->height() == 1 && rgb->channels() == 3,
              "RGB normal map dimensions/channels are wrong.");
      auto rgba = set.acquire("maps/rgba.tga");
      require(rgba->channels() == 3,
              "RGBA normal map did not discard alpha as vector data.");

      auto texture = std::dynamic_pointer_cast<mpp::Texture>(rgb->texture());
      require(texture && texture->isLoaded() && texture->getMipLevels() > 1,
              "Normal map did not create a loaded, mipmapped GPU texture.");
      auto stream = std::dynamic_pointer_cast<mpp::TextureStream>(texture->getResourceStream());
      require(stream && stream->getBitsPerPixel() == 24 && stream->getPixelFormat() == GL_RGB &&
                  stream->getParams().colourSpace == mpp::TextureColourSpace::Linear &&
                  stream->getParams().wrap == GL_REPEAT &&
                  stream->getParams().minFilter == GL_LINEAR_MIPMAP_LINEAR &&
                  stream->getParams().magFilter == GL_LINEAR,
              "Normal-map sampler settings do not enforce linear/repeat/trilinear semantics.");
      textureName = texture->getName();

      writeRgbPpm(root / "maps" / "normal.ppm", 42);
      auto oldTexture = rgb->texture();
      set.reload("maps/normal.ppm");
      require(rgb->texture() != oldTexture, "Explicit reload did not replace the decoded GPU texture.");

      requireRejected(set, root / "maps" / "normal.ppm", "Absolute path");
      requireRejected(set, "../outside.ppm", "Escaping path");
      requireRejected(set, "maps/missing.ppm", "Missing file");
      requireRejected(set, "maps/invalid.bin", "Invalid image");
      requireRejected(set, "maps/zero.bin", "Zero-sized image");
      requireRejected(set, "maps/gray.pgm", "Grayscale image");
      requireRejected(set, "maps/gray-alpha.tga", "Grayscale-plus-alpha image");
      requireRejected(set, "maps/huge.ppm", "Oversized image");
    }
    require(!resources.getResource(textureName, true),
            "Destroying the normal-map resource set did not release its texture.");
    result = 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
  }
  if (context) SDL_GL_DestroyContext(context);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
  fs::remove_all(root);
  if (result == 0) std::cout << "NormalMapResourceSet smoke test passed\n";
  return result;
}
