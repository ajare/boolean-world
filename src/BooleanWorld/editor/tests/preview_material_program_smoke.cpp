// Not a unit test - creates a real GL context, renders a floor quad through
// PreviewMaterialProgram exactly as Preview3D.cpp does, and reads the pixels
// back. Verifies the whole path actually puts colour on screen, rather than
// only that the shader compiles: a black preview compiles perfectly well.
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
std::vector<editor::PreviewGpuVertex> floorQuad() {
  auto corner = [](float x, float z) {
    editor::PreviewGpuVertex vertex;
    vertex.px = x;
    vertex.py = 0.0f;
    vertex.pz = z;
    vertex.ny = 1.0f;
    vertex.u = x;
    vertex.v = z;
    return vertex;
  };
  auto a = corner(-100.0f, -100.0f);
  auto b = corner(100.0f, -100.0f);
  auto c = corner(100.0f, 100.0f);
  auto d = corner(-100.0f, 100.0f);
  return {a, b, c, c, d, a};
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

  program.begin(view, projection, cameraPosition, cameraPosition, 0.0f);
  program.setMaterial(0, {});
  program.draw(floorQuad());
  program.end();

  auto error = glGetError();
  if (error != GL_NO_ERROR) {
    printf("GL error after draw: 0x%x\n", error);
    return 1;
  }

  std::vector<unsigned char> pixels((size_t)kWidth * kHeight * 4);
  glReadPixels(
      0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  size_t litPixels = 0;
  int maxChannel = 0;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    int r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
    int brightest = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (brightest > maxChannel) {
      maxChannel = brightest;
    }
    if (brightest > 8) {
      ++litPixels;
    }
  }

  auto total = (size_t)kWidth * kHeight;
  printf(
      "lit pixels: %zu / %zu (brightest channel %d)\n", litPixels, total,
      maxChannel);

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
  if (litPixels < total / 10) {
    printf("FAILED: the material shader rendered a black frame\n");
    return 1;
  }
  printf("PASSED: the material shader rendered visible colour\n");
  return 0;
}
