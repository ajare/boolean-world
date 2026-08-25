// Not a unit test - creates a real GL context, renders a floor quad through
// PreviewMaterialProgram exactly as Preview3D.cpp does, and reads the pixels
// back. Verifies the whole path actually puts colour on screen, rather than
// only that the shader compiles: a black preview compiles perfectly well.
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "PreviewMaterialProgram.h"

spdlog::logger* gLogger{nullptr};

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 128;

// A floor quad on the y=0 plane, normal pointing up, big enough to fill the
// view from the camera position used below.
std::vector<editor::PreviewGpuVertex> floorQuad(float tintBlue) {
  auto corner = [tintBlue](float x, float z) {
    editor::PreviewGpuVertex vertex;
    vertex.px = x;
    vertex.py = 0.0f;
    vertex.pz = z;
    vertex.ny = 1.0f;
    vertex.u = x;
    vertex.v = z;
    vertex.b = tintBlue;
    return vertex;
  };
  auto a = corner(-100.0f, -100.0f);
  auto b = corner(100.0f, -100.0f);
  auto c = corner(100.0f, 100.0f);
  auto d = corner(-100.0f, 100.0f);
  return {a, b, c, c, d, a};
}

// A vertical wall quad standing on the z=0 plane and facing the camera, to
// check the tint is not somehow particular to horizontal surfaces.
std::vector<editor::PreviewGpuVertex> wallQuad(float tintBlue) {
  auto corner = [tintBlue](float x, float height) {
    editor::PreviewGpuVertex vertex;
    vertex.px = x;
    vertex.py = height;
    vertex.pz = 0.0f;
    vertex.nz = 1.0f;
    vertex.u = x;
    vertex.v = height;
    vertex.b = tintBlue;
    return vertex;
  };
  auto a = corner(-100.0f, 0.0f);
  auto b = corner(100.0f, 0.0f);
  auto c = corner(100.0f, 60.0f);
  auto d = corner(-100.0f, 60.0f);
  return {a, b, c, c, d, a};
}

struct FrameStats {
  size_t litPixels{};
  int maxChannel{};
  double meanBlue{};
  double meanRed{};
};

std::vector<unsigned char> capturePixels(int width, int height) {
  std::vector<unsigned char> pixels((size_t)width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  return pixels;
}

// How far apart two frames are, averaged over every colour channel.
double meanAbsoluteDifference(
    std::vector<unsigned char> const& first,
    std::vector<unsigned char> const& second) {
  if (first.size() != second.size() || first.empty()) {
    return 0.0;
  }
  double total = 0.0;
  size_t counted = 0;
  for (size_t i = 0; i < first.size(); i += 4) {
    for (size_t channel = 0; channel < 3; ++channel) {
      total += std::abs((int)first[i + channel] - (int)second[i + channel]);
      ++counted;
    }
  }
  return counted > 0 ? total / (double)counted : 0.0;
}

FrameStats measureFrame(std::vector<unsigned char> const& pixels) {
  FrameStats stats;
  double blueTotal = 0.0, redTotal = 0.0;
  size_t counted = 0;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    int r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
    int brightest = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (brightest > stats.maxChannel) {
      stats.maxChannel = brightest;
    }
    if (brightest > 8) {
      ++stats.litPixels;
      blueTotal += b;
      redTotal += r;
      ++counted;
    }
  }
  if (counted > 0) {
    stats.meanBlue = blueTotal / (double)counted;
    stats.meanRed = redTotal / (double)counted;
  }
  return stats;
}

}  // namespace

int main() {
  auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  gLogger = new spdlog::logger("smoke", {consoleSink});
  gLogger->set_level(spdlog::level::trace);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  auto* window = SDL_CreateWindow(
      "smoke", 64, 64, (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
  if (!window) {
    printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }
  auto context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, context);

  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    printf("glewInit failed\n");
    return 1;
  }

  printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
  printf("GL_SHADING_LANGUAGE_VERSION: %s\n",
         glGetString(GL_SHADING_LANGUAGE_VERSION));

  editor::PreviewMaterialProgram program;
  if (!program.ensureReady()) {
    printf("PreviewMaterialProgram: FAILED to become ready\n");
    return 1;
  }
  printf("PreviewMaterialProgram: ready\n");

  // world_pbr_2d.frag is the game's other consumer of MATERIAL_PARAMS - the
  // shader HorizontalMaterials: 2d selects. It only shares a vertex shader
  // with the 3D one, not the fragment shader itself, so a syntax mistake
  // there would otherwise go unnoticed until someone actually plays with
  // that video setting on.
  editor::PreviewMaterialProgram program2d("shaders/world_pbr_2d.frag");
  if (!program2d.ensureReady()) {
    printf("PreviewMaterialProgram (world_pbr_2d.frag): FAILED\n");
    return 1;
  }
  printf("PreviewMaterialProgram (world_pbr_2d.frag): ready\n");

  // Render offscreen so the result does not depend on a visible window.
  GLuint colourTexture{}, depthBuffer{}, framebuffer{};
  glGenTextures(1, &colourTexture);
  glBindTexture(GL_TEXTURE_2D, colourTexture);
  glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenRenderbuffers(1, &depthBuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kWidth, kHeight);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colourTexture, 0);
  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
  GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &drawBuffer);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    printf("offscreen framebuffer incomplete\n");
    return 1;
  }

  glViewport(0, 0, kWidth, kHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  // Reproduce the state ImGui leaves set when it invokes a draw callback: its
  // own VAO bound and its own vertex buffer bound to GL_ARRAY_BUFFER. Without
  // this, a client-memory glVertexAttribPointer would work here and the test
  // would pass while the real preview rendered black - GL reinterprets such a
  // pointer as an offset into whatever buffer is bound.
  GLuint foreignVertexArray{}, foreignBuffer{};
  glGenVertexArrays(1, &foreignVertexArray);
  glBindVertexArray(foreignVertexArray);
  glGenBuffers(1, &foreignBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, foreignBuffer);
  std::vector<float> junk(1024, 0.0f);
  glBufferData(
      GL_ARRAY_BUFFER, (GLsizeiptr)(junk.size() * sizeof(float)), junk.data(),
      GL_STREAM_DRAW);

  // Look down at the floor quad from a height, matching the scale the
  // preview's grounded eye position works at.
  glm::vec3 cameraPosition{0.0f, 20.0f, 50.0f};
  auto view = glm::lookAt(
      cameraPosition, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
  auto projection = glm::perspective(
      glm::radians(60.0f), (float)kWidth / (float)kHeight, 0.1f, 1000000.0f);

  // Marble's registry defaults - matching MaterialRegistry.h's
  // MaterialParams[0], each the literal world_pbr.frag's MarbleParams used
  // to hardcode at that slot. Passing {} here instead would zero
  // fineDetailScale, which marbleTexture divides by - now that the shader
  // actually reads these, an all-zero params array is a NaN, not a neutral
  // placeholder.
  std::array<float, BW_MATERIAL_PARAMS_MAX> marbleDefaults{
      1.35f, 5.0f, 12.0f, 0.025f, 0.2f, 0.35f, 0.42f, 0.72f};

  auto renderPixels = [&](std::vector<editor::PreviewGpuVertex> const& vertices,
                          uint32_t materialIndex,
                          std::array<float, BW_MATERIAL_PARAMS_MAX> const&
                              params) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    program.begin(view, projection, cameraPosition, cameraPosition, 0.0f);
    program.setMaterial(materialIndex, params);
    program.draw(vertices);
    program.end();
    return capturePixels(kWidth, kHeight);
  };
  auto render = [&](std::vector<editor::PreviewGpuVertex> const& vertices) {
    return measureFrame(renderPixels(vertices, 0, marbleDefaults));
  };

  // White vertex colours must leave the material exactly as authored; a
  // reduced blue channel must tint it without touching red.
  auto untinted = render(floorQuad(1.0f));
  auto tinted = render(floorQuad(0.35f));
  // The value the preview actually tints with, so this measures the real
  // thing rather than a placeholder.
  auto untintedWall = render(wallQuad(1.0f));
  auto tintedWall = render(wallQuad(0.35f));

  // Moving one material parameter must change what is drawn, or the editor's
  // sliders would move with nothing happening in the preview behind them.
  auto atDefaults = renderPixels(floorQuad(1.0f), 0, marbleDefaults);
  auto altered = marbleDefaults;
  // fbm_scale rescales the whole input coordinate, so it dominates every
  // downstream computation - the parameter least likely to hide in a subtle
  // blend and most likely to prove the wiring by its absence.
  altered[7] = 0.05f;  // fbm_scale, registry default 0.72
  auto atAltered = renderPixels(floorQuad(1.0f), 0, altered);
  auto parameterShift = meanAbsoluteDifference(atDefaults, atAltered);

  // Slate/Sandstone/Limestone (material indices 2/3/4), wired this batch.
  // base_scale rescales their whole input coordinate the same way Marble's
  // fbm_scale does, so it is the parameter most certain to prove the wiring.
  auto stoneLikeShift = [&](uint32_t materialIndex,
                             std::array<float, BW_MATERIAL_PARAMS_MAX> const&
                                 defaults) {
    auto base = renderPixels(floorQuad(1.0f), materialIndex, defaults);
    auto changed = defaults;
    changed[0] *= 0.2f;  // base_scale
    auto shifted = renderPixels(floorQuad(1.0f), materialIndex, changed);
    return meanAbsoluteDifference(base, shifted);
  };
  auto slateShift = stoneLikeShift(2, {0.72f, 0.35f});
  auto sandstoneShift = stoneLikeShift(3, {0.58f, 18.0f});
  auto limestoneShift = stoneLikeShift(4, {0.66f, 0.38f});

  auto error = glGetError();
  if (error != GL_NO_ERROR) {
    printf("GL error after draw: 0x%x\n", error);
    return 1;
  }

  auto total = (size_t)kWidth * kHeight;
  printf(
      "lit pixels: %zu / %zu (brightest channel %d)\n", untinted.litPixels,
      total, untinted.maxChannel);
  printf(
      "floor: untinted red %.1f blue %.1f; tinted red %.1f blue %.1f\n",
      untinted.meanRed, untinted.meanBlue, tinted.meanRed, tinted.meanBlue);
  printf(
      "wall:  untinted red %.1f blue %.1f; tinted red %.1f blue %.1f\n",
      untintedWall.meanRed, untintedWall.meanBlue, tintedWall.meanRed,
      tintedWall.meanBlue);
  printf(
      "fbm_scale 0.72 -> 0.05 shifts the image by %.1f per channel\n",
      parameterShift);
  printf(
      "base_scale shifts: Slate %.1f, Sandstone %.1f, Limestone %.1f\n",
      slateShift, sandstoneShift, limestoneShift);

  glBindVertexArray(0);
  glDeleteBuffers(1, &foreignBuffer);
  glDeleteVertexArrays(1, &foreignVertexArray);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteRenderbuffers(1, &depthBuffer);
  glDeleteTextures(1, &colourTexture);

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  // The quad covers a large part of the view; requiring a solid fraction of
  // the frame to be lit catches both "nothing drew" and "drew but black".
  if (untinted.litPixels < total / 10) {
    printf("FAILED: the material shader rendered a black frame\n");
    return 1;
  }
  // Holding back blue must visibly darken blue and leave red alone. Without
  // the shader honouring vertex colour at all, the two frames are identical,
  // so any real reduction proves the tint arrived. It lands well short of the
  // 30% taken off the albedo because gamma compresses it and the specular
  // terms are not tinted, which is why this asks only for a clear margin.
  if (!(tinted.meanBlue < untinted.meanBlue * 0.97)) {
    printf("FAILED: the vertex colour tint did not reduce blue\n");
    return 1;
  }
  if (std::abs(tinted.meanRed - untinted.meanRed) > untinted.meanRed * 0.02) {
    printf("FAILED: the tint disturbed red, which it should leave alone\n");
    return 1;
  }
  // Vertical surfaces must tint as readily as horizontal ones.
  if (!(tintedWall.meanBlue < untintedWall.meanBlue * 0.97)) {
    printf("FAILED: the vertex colour tint did not reduce blue on a wall\n");
    return 1;
  }
  // world_pbr.frag now reads MATERIAL_PARAMS via MarbleParams, so moving a
  // parameter must change the image - this is what the editor's sliders
  // ultimately rely on actually doing anything.
  if (!(parameterShift > 1.0)) {
    printf(
        "FAILED: changing a material parameter did not change the image\n");
    return 1;
  }
  if (!(slateShift > 1.0)) {
    printf("FAILED: changing Slate's base_scale did not change the image\n");
    return 1;
  }
  if (!(sandstoneShift > 1.0)) {
    printf(
        "FAILED: changing Sandstone's base_scale did not change the image\n");
    return 1;
  }
  if (!(limestoneShift > 1.0)) {
    printf(
        "FAILED: changing Limestone's base_scale did not change the image\n");
    return 1;
  }
  printf(
      "PASSED: the material renders as authored, honours vertex tint, and "
      "responds to its parameters\n");
  return 0;
}
