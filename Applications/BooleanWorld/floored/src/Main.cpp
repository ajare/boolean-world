#define NOMINMAX
#include <Windows.h>
#undef NOMINMAX

#include <filesystem>
#include <fstream>

#include <GL/glew.h>
#include <nfd/nfd.h>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <fmod/core/fmod.hpp>
#include <fmod/studio/fmod_studio.hpp>
#pragma warning(push)
#pragma warning(disable : 4505)
#include <fmod/core/fmod_errors.h>
#pragma warning(pop)

#include <SDL3/SDL.h>
// SDL2main is gone in SDL3. These stay WIN32 (subsystem:windows) apps, so
// SDL_main.h supplies the WinMain shim that forwards to main().
#include <SDL3/SDL_main.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <core/WorldData.h>

#include <common/GameDefines.h>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "IconsFontAwesome5.h"

#include "Defines.h"
#include "Helpers.h"
#include "UI.h"
#include "Render.h"
#include "Settings.h"
#include "ExitApplicationException.h"

using namespace std;

wp::Vector2 gViewOffset{0.0f, 0.0f};
float gViewZoom{1.0f};

spdlog::logger* gLogger{nullptr};
SDL_Window* gWindow{nullptr};
SDL_GLContext gContext;

static floored::Settings gSettings;

SDL_Window* createWindow() {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    printf("Error: %s\n", SDL_GetError());
    return nullptr;
  }

  // GL 3.0 + GLSL 130
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

  // From 2.0.18: Enable native IME.

  // Create window with graphics context
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Window* window = SDL_CreateWindow("Editor", FE_WINDOW_WIDTH, FE_WINDOW_HEIGHT, window_flags);

  gLogger->info("Window created");

  return window;
}

SDL_GLContext createContext(SDL_Window* window) {
  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1);  // Enable vsync

  gLogger->info("OpenGL context created");

  return gl_context;
}

void setupImGui(SDL_Window* window, SDL_GLContext context) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL3_InitForOpenGL(window, context);

  const char* glsl_version = "#version 130";
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
  // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
  // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
  // - Read 'docs/FONTS.md' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
  // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
  // io.Fonts->AddFontDefault();
  // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
  // IM_ASSERT(font != NULL);

  gLogger->info("ImGui set up");
}

void setupLogging() {
  auto fileSink = make_shared<spdlog::sinks::basic_file_sink_mt>("logs/floored.log", true);

#ifdef _DEBUG
  auto consoleSink = make_shared<spdlog::sinks::stdout_color_sink_mt>();
  consoleSink->set_level(spdlog::level::debug);

  fileSink->set_level(spdlog::level::debug);

  gLogger = new spdlog::logger("floored", {consoleSink, fileSink});
#else
  fileSink->set_level(spdlog::level::info);

  gLogger = new spdlog::logger("floored", {fileSink});
#endif

  gLogger->set_level(spdlog::level::debug);
}

void initialise() {
  setupLogging();

  //
  // Set up SDL
  //
  gWindow = createWindow();
  gContext = createContext(gWindow);

  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    throw exception("GLEW initialisation failed");
  }

  //
  // Set up ImGui
  //
  setupImGui(gWindow, gContext);
}

void setup() {
  // Set up NFD (file dialogs)
  NFD_Init();

  // ImGui extra twiddling
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

  io.Fonts->AddFontDefault();

  float baseFontSize = 13.0f;                       // 13.0f is the size of the default font. Change to the font size you use.
  float iconFontSize = baseFontSize * 2.0f / 3.0f;  // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly

  static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.GlyphMinAdvanceX = iconFontSize;
  io.Fonts->AddFontFromFileTTF(FONT_ICON_FILE_NAME_FAS, iconFontSize, &icons_config, icons_ranges);
  io.Fonts->Build();

  Clipper2Lib::WmInitialiseAllocators(4, 16 * 1024 * 1024);
}

void shutdown() {
  gLogger->info("Shutting down");

  Clipper2Lib::WmDestroyAllocators();

  // ImGui
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  // Platform
  SDL_GL_DestroyContext(gContext);
  SDL_DestroyWindow(gWindow);
  SDL_Quit();

  NFD_Quit();

  delete gLogger;
  gLogger = nullptr;
}

bool processEvents(SDL_Window* window) {
  // Poll and handle events (inputs, window resize, etc.)
  // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
  // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
  // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
  // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
  bool done = false;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) {
      done = true;
    }
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
      done = true;
    }
  }

  return done;
}

void handleContinuousKeyboardInput(uint64_t updateTimeMicros) {
  const float MoveSpeed{500.0f};
  const float ZoomSpeed{0.5f};

  auto const& io = ImGui::GetIO();

  if (io.WantCaptureKeyboard) {
    return;
  }

  if (!floored::Document::instance()->isActive()) {
    return;
  }

  float frameTime = updateTimeMicros / 1000000.0f;

  float moveSpeed = MoveSpeed * frameTime * (io.KeyShift ? 4.0f : 1.0f);
  float zoomSpeed = ZoomSpeed * frameTime * (io.KeyShift ? 4.0f : 1.0f);

  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
    gViewOffset.x += FE_WINDOW_WIDTH;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
    gViewOffset.x -= FE_WINDOW_WIDTH;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    gViewOffset.y += FE_WINDOW_HEIGHT;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    gViewOffset.y -= FE_WINDOW_HEIGHT;
  }

  if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
    gViewOffset.x += moveSpeed;
  }
  if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
    gViewOffset.x -= moveSpeed;
  }
  if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
    gViewOffset.y += moveSpeed;
  }
  if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
    gViewOffset.y -= moveSpeed;
  }
  if (ImGui::IsKeyDown(ImGuiKey_A)) {
    gViewZoom -= zoomSpeed;
  }
  if (ImGui::IsKeyDown(ImGuiKey_Z)) {
    gViewZoom += zoomSpeed;
  }

  // Clamp to world bounds
  auto const& worldBounds = floored::Document::instance()->getWorld()->getExtents();
  wp::Vector2 minExtent, maxExtent;

  worldBounds.getExtents(minExtent, maxExtent);

  gViewOffset.x = clamp(gViewOffset.x, minExtent.x + FE_WINDOW_WIDTH * 0.5f, maxExtent.x - FE_WINDOW_WIDTH * 0.5f);
  gViewOffset.y = clamp(gViewOffset.y, minExtent.y + FE_WINDOW_HEIGHT * 0.5f, maxExtent.y - FE_WINDOW_HEIGHT * 0.5f);
  gViewZoom = clamp(gViewZoom, 0.1f, 1.0f);
}

void run() {
  ImVec4 clearColour = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);  // ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
  ImGuiIO& io = ImGui::GetIO();

  //
  // Main loop
  //
  LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
  LARGE_INTEGER Frequency;

  QueryPerformanceFrequency(&Frequency);
  QueryPerformanceCounter(&StartingTime);

  bool done = false, showDemoWindow = false;
  while (!done) {
    // Get elapsed time
    QueryPerformanceCounter(&EndingTime);
    ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
    ElapsedMicroseconds.QuadPart *= 1000000;
    ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

    StartingTime = EndingTime;

    auto updateTimeMicros = ElapsedMicroseconds.QuadPart;

    // Events
    done = processEvents(gWindow);

    // Logic
    if (floored::Document::instance()->isActive()) {
      auto doc = floored::Document::instance();
    }

    glViewport(0, 0, FE_WINDOW_WIDTH, FE_WINDOW_HEIGHT);
    glClearColor(clearColour.x * clearColour.w, clearColour.y * clearColour.w, clearColour.z * clearColour.w, clearColour.w);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();

    handleContinuousKeyboardInput(updateTimeMicros);

    floored::renderUI(floored::Document::instance(), gSettings);

    // Rendering
    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(gWindow);
  }
}

void outputToDebugger(string const& msg) {
#ifdef _DEBUG
  char const* msgc = msg.c_str();

  size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msgc, (int)strlen(msgc), 0, 0);
  wstring ret(reqLength, L'\0');

  ::MultiByteToWideChar(CP_UTF8, 0, msgc, (int)strlen(msgc), &ret[0], (int)ret.length());
  OutputDebugString(ret.c_str());
#endif
}

void outputException(string const& msg) {
  gLogger->critical(msg);
  outputToDebugger(msg);
}

//
// Entrypoint
//
int main(int, char**) {
  int exitCode{0};

  initialise();

  try {
    setup();
    run();
  } catch (ExitApplicationException& e) {
    exitCode = e.getExitCode();
    outputException(e.getMessage());
  } catch (exception& e) {
    exitCode = 1;
    outputException(e.what());
  }

  shutdown();

  return exitCode;
}
