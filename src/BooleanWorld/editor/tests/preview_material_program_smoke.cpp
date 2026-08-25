// Not a unit test - creates a real GL context and drives
// PreviewMaterialProgram exactly as Preview3D.cpp does, so shader
// compile/link failures show up as readable errors instead of a black
// preview window with no feedback.
#include <cstdio>
#include <memory>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "PreviewMaterialProgram.h"

spdlog::logger* gLogger{nullptr};

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
  bool ready = program.ensureReady();
  printf(ready ? "PreviewMaterialProgram: ready\n"
               : "PreviewMaterialProgram: FAILED\n");

  if (ready) {
    program.begin(
        glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.0f), glm::vec3(0.0f),
        0.0f);
    program.setMaterial(0, {});
    program.end();
    auto error = glGetError();
    if (error != GL_NO_ERROR) {
      printf("GL error after begin/setMaterial/end: 0x%x\n", error);
      ready = false;
    } else {
      printf("begin/setMaterial/end: no GL errors\n");
    }
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return ready ? 0 : 1;
}
