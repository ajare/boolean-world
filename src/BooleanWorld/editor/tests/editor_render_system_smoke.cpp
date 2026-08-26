// Not a unit test - creates a real GL context and proves EditorRenderSystem's
// bootstrap/teardown sequence (mpp::Logger -> mpp::RenderSystem ->
// mpp::ResourceManager -> createCoreResources -> the manifest-driven
// application ResourceManager) works and tears down cleanly, matching the
// single construct-then-destroy lifetime the class actually has: at most one
// instance may exist per process, ever - even sequentially. See
// EditorRenderSystem.h for why (a process-wide static
// ResourceDefinitionFactory registry that wp::application::resourcesystem::
// ResourceManager's destructor never clears; confirmed against this same
// library by hitting "DefinitionFactory for resource type 'TextFile' ... is
// already registered" on a second construct/destroy cycle while writing this
// test). See GitHub issue #268. This covers the subsystem on its own; the
// preview's Scene/pipeline/WorldRenderer stack on top of it has its own smoke
// test - preview_render_stack_smoke.cpp.
#include <cstdint>
#include <cstdio>
#include <exception>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <mpp/ResourceManager.h>

#include "EditorRenderSystem.h"

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 64;

}  // namespace

int main() {
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
      "smoke", kWidth, kHeight, (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
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

  try {
    editor::EditorRenderSystem renderSystem(kWidth, kHeight);

    uint32_t numResources{}, numDeclared{}, numCreated{}, numLoaded{};
    renderSystem.renderResourceManager()->getResourceCounts(
        numResources, numDeclared, numCreated, numLoaded);
    printf(
        "resources=%u declared=%u created=%u loaded=%u\n", numResources,
        numDeclared, numCreated, numLoaded);
    renderSystem.renderResourceManager()->dumpResources(
        "editor-render-system-smoke-resources.csv");

    if (numLoaded == 0) {
      printf("FAILED: createCoreResources() loaded zero core resources\n");
      return 1;
    }
    // renderSystem destructs here, at scope exit - proving the documented
    // teardown order (application ResourceManager -> destroyCoreResources()
    // -> mpp::ResourceManager -> mpp::RenderSystem -> mpp::Logger) runs
    // without crashing.
  } catch (std::exception const& ex) {
    printf("FAILED: exception: %s\n", ex.what());
    return 1;
  }

  auto error = glGetError();
  if (error != GL_NO_ERROR) {
    printf("GL error after teardown: 0x%x\n", error);
    return 1;
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  printf("PASSED: EditorRenderSystem bootstraps and tears down cleanly\n");
  return 0;
}
