#define NOMINMAX

#include <Windows.h>
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

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#ifndef IMGUI_HAS_DOCK
#error "The editor requires Dear ImGui's docking branch"
#endif
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "IconsFontAwesome5.h"

#include "Defines.h"
#include "Document.h"
#include "Settings.h"
#include "Render.h"
#include "UI.h"
#include "UiHelpers.h"
#include "Undo.h"
#include "Actions.h"
#include "EditorException.h"
#include "ExitApplicationException.h"
#include "AppHelpers.h"
#include "HoverableType.h"
#include "PrimitiveFieldPreview.h"

wp::Vector2 gViewOffset{0.0f, 0.0f};

// World units per screen pixel scale of the world view. 1 = unzoomed (the
// original, pre-zoom 1:1 mapping); >1 zooms in, <1 zooms out.
float gViewZoom{1.0f};

// The screen-space rect the World window currently occupies, set each frame
// by renderWidgets() before renderWorld()/renderMiniMap() draw into it. Read
// one frame late by input handling below, same as io.WantCaptureMouse.
wp::Vector2 gWorldViewScreenOrigin{0.0f, 0.0f};
wp::Vector2 gWorldViewSize{ED_WINDOW_WIDTH, ED_WINDOW_HEIGHT};

spdlog::logger* gLogger{nullptr};
SDL_Window* gWindow{nullptr};
SDL_GLContext gContext{nullptr};

editor::Settings gEditorSettings;
editor::HoverableType gHoveredType{editor::HoverableType::None};
std::vector<uint32_t> gHoveredIndices;
editor::EditorInteraction gEditorInteraction;

using namespace std;

map<string, string> gHelpFiles;

SDL_Window* createWindow() {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    printf("Error: %s\n", SDL_GetError());
    return nullptr;
  }

  // GL 3.0 + GLSL 130
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  // No profile mask: MassivePolyPusher's 2D text path enables GL_POINT_SPRITE,
  // which a core profile rejects with GL_INVALID_ENUM. See WindowSDL::create.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

  // From 2.0.18: Enable native IME.

  // Create window with graphics context
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Window* window = SDL_CreateWindow("Editor", ED_WINDOW_WIDTH, ED_WINDOW_HEIGHT, window_flags);

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

void outputToDebugger(string const& msg);

// Declared by imconfig.h, which redirects IM_ASSERT here.
void EditorImGuiAssertFailed(char const* expr, char const* file, int line) {
  auto msg = format("ImGui assertion failed: {} at {}:{}", expr, file, line);

  if (gLogger) {
    gLogger->critical(msg);
    gLogger->flush();
  }

  outputToDebugger(msg);
  abort();
}

void setupLogging() {
  auto fileSink = make_shared<spdlog::sinks::basic_file_sink_mt>("logs/editor.log", true);

#ifdef _DEBUG
  auto consoleSink = make_shared<spdlog::sinks::stdout_color_sink_mt>();
  consoleSink->set_level(spdlog::level::debug);

  fileSink->set_level(spdlog::level::debug);

  gLogger = new spdlog::logger("editor", {consoleSink, fileSink});
#else
  fileSink->set_level(spdlog::level::info);

  gLogger = new spdlog::logger("editor", {fileSink});
#endif

  gLogger->set_level(spdlog::level::debug);

  // abort() does not flush the CRT's file buffers, so without this an assert
  // silently discards every line logged since the last flush - which makes the
  // log look like it stopped somewhere it did not.
  gLogger->flush_on(spdlog::level::debug);
}

map<string, string> loadHelpFiles(string const& dir) {
  map<string, string> data;

  // Help is optional editor content. Visual Studio may launch the executable
  // with the project directory as its working directory rather than the
  // staged runtime directory, so do not turn a missing help directory into a
  // startup exception.
  error_code error;
  filesystem::directory_iterator entries(dir, error);
  if (error) {
    gLogger->warn("Could not load help files from '{}': {}", dir, error.message());
    return data;
  }

  for (const auto& entry : entries) {
    auto filepath = entry.path();

    if (filepath.extension() == ".md") {
      auto name = filepath.stem();

      ifstream file(filepath);

      ostringstream oss;
      oss << file.rdbuf();

      string content = oss.str();

      data[name.string()] = content;
    }
  }

  return data;
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

  // Load help files
  gHelpFiles = loadHelpFiles("doc/core");
  gHelpFiles.merge(loadHelpFiles("doc/editor"));
  gLogger->debug("Help files loaded");
}

void setup() {
  // Set up NFD (file dialogs)
  NFD_Init();

  // ImGui extra twiddling
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  gLogger->debug("NFD initialised");

  float baseFontSize = 13.0f;                       // 13.0f is the size of the default font. Change to the font size you use.
  float iconFontSize = baseFontSize * 2.0f / 3.0f;  // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly

  // SizePixels must be set explicitly. From ImGui 1.92 a bare AddFontDefault()
  // gives the font an implicit reference size, and merging a font that has an
  // explicit one into it asserts.
  ImFontConfig default_config;
  default_config.SizePixels = baseFontSize;
  io.Fonts->AddFontDefault(&default_config);
  gLogger->debug("Default font added");

  static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.GlyphMinAdvanceX = iconFontSize;
  io.Fonts->AddFontFromFileTTF(FONT_ICON_FILE_NAME_FAS, iconFontSize, &icons_config, icons_ranges);
  gLogger->debug("Icon font added");

  io.Fonts->Build();
  gLogger->debug("Fonts built");

  // Installed once, ahead of any World: every World the Document builds
  // generates through it, so a Primitive hidden by the step filter contributes
  // no geometry either.
  editor::applyStepVisibilityFilter(editor::Document::instance(), gEditorSettings);
}

void shutdown() {
  editor::getPrimitiveFieldPreview().close();

  // ImGui
  if (ImGui::GetCurrentContext()) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
  }

  // Platform
  if (gContext) {
    SDL_GL_DestroyContext(gContext);
  }
  if (gWindow) {
    SDL_DestroyWindow(gWindow);
  }
  SDL_Quit();

  NFD_Quit();

  if (gLogger) {
    gLogger->info("Shutting down");
    gLogger->flush();
    delete gLogger;
    gLogger = nullptr;
  }
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

editor::MouseButtonStatus getMouseButtonStatus() {
  editor::MouseButtonStatus status{};

  auto const& io = ImGui::GetIO();

  static ImVec2 frameDragDelta[2];
  static bool ownedByWorld[2];

  for (int i = 0; i < 2; ++i) {
    // A gesture that began in the world view (pressed while ImGui didn't
    // want the mouse) owns the button until release, even if the cursor
    // has since crossed onto a docked panel. Otherwise a release over a
    // panel is never reported and the drag state never clears.
    bool const trackThisFrame = ownedByWorld[i] || !io.WantCaptureMouse;

    if (!trackThisFrame) {
      status.state[i] = editor::MouseButtonStatus::State::Unavailable;
      status.dragging[i] = false;
      status.dragDelta[i] = {0, 0};
      continue;
    }

    if (io.MouseClicked[i]) {
      frameDragDelta[i] = ImGui::GetMouseDragDelta(i);
      status.state[i] = editor::MouseButtonStatus::State::Clicked;
      ownedByWorld[i] = true;
    } else if (io.MouseReleased[i]) {
      status.state[i] = editor::MouseButtonStatus::State::Released;
      ownedByWorld[i] = false;
    } else if (io.MouseDown[i]) {
      status.state[i] = editor::MouseButtonStatus::State::Down;
    }

    status.dragging[i] = ImGui::IsMouseDragging(i, 2);
    status.dragDelta[i] = ImGui::GetMouseDragDelta(i) - frameDragDelta[i];

    frameDragDelta[i] = ImGui::GetMouseDragDelta(i);
  }

  return status;
}

editor::PointerInput readPointerInput(
    editor::Document* doc,
    editor::MouseButtonStatus const& mouseStatus) {
  auto const& io = ImGui::GetIO();
  auto mouseScreen = ImGui::GetMousePos();
  auto miniMapBounds = getMiniMapBounds(doc);
  miniMapBounds.setPosition(
      miniMapBounds.getMinExtent() + gWorldViewScreenOrigin);

  editor::PointerInput input;
  input.screenPosition = {mouseScreen.x, mouseScreen.y};
  input.worldPosition = editor::getMouseWorldPosition();
  auto const& boxStart = gEditorInteraction.getBoxSelectStartScreen();
  input.boxSelectStartWorld = editor::screenToWorldPosition(
      ImVec2{boxStart.x, boxStart.y});
  input.dragDelta = {
      mouseStatus.dragDelta[editor::MouseButtonStatus::Left].x,
      mouseStatus.dragDelta[editor::MouseButtonStatus::Left].y};
  input.zoom = gViewZoom;
  input.cursorInWorldView = editor::mouseInteractingWithBackground();
  input.cursorInMiniMap = doc->getWorld() && gEditorSettings.renderMiniMap &&
                          miniMapBounds.pointInside(mouseScreen.x, mouseScreen.y);
  input.leftClicked =
      mouseStatus.state[editor::MouseButtonStatus::Left] ==
      editor::MouseButtonStatus::State::Clicked;
  input.leftDown =
      mouseStatus.state[editor::MouseButtonStatus::Left] ==
      editor::MouseButtonStatus::State::Down;
  input.leftReleased =
      mouseStatus.state[editor::MouseButtonStatus::Left] ==
      editor::MouseButtonStatus::State::Released;
  input.leftDragging = mouseStatus.dragging[editor::MouseButtonStatus::Left];
  input.control = io.KeyCtrl;
  input.shift = io.KeyShift;
  input.alt = io.KeyAlt;
  return input;
}

void handleSelections(
    editor::Document* doc,
    bw::core::WorldData const* worldData,
    editor::Settings& settings,
    editor::PointerInput const& input) {
  if (input.cursorInWorldView && !input.cursorInMiniMap &&
      doc->meshDrawToolArmed()) {
    auto position = editor::Document::snapMeshDrawPosition(
        input.worldPosition, settings.showGrid, settings.gridSize);
    if (doc->meshDrawClickWouldClose(position, settings)) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
  }

  gEditorInteraction.updateSelection(doc, worldData, settings, input);

  auto const& hover = gEditorInteraction.getHover();
  gHoveredType = hover.type;
  gHoveredIndices = hover.indices;

  if (gEditorInteraction.boxSelectDragging()) {
    auto const& start = gEditorInteraction.getBoxSelectStartScreen();
    auto current = input.screenPosition;
    ImVec2 rectMin{min(start.x, current.x), min(start.y, current.y)};
    ImVec2 rectMax{max(start.x, current.x), max(start.y, current.y)};
    auto drawList = ImGui::GetForegroundDrawList();
    drawList->AddRectFilled(rectMin, rectMax, IM_COL32(180, 200, 255, 40));
    drawList->AddRect(rectMin, rectMax, IM_COL32(180, 200, 255, 220));
  }

  if (hover.type != editor::HoverableType::None) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }
}

// Blender-style navigation for the world view: middle-mouse-button drag pans
// (also Shift+Alt+left-mouse-button drag, Blender's alternate binding for
// trackpad/one-button setups), and the scroll wheel zooms toward the cursor
// (as in Blender's 2D editors - Shader/UV/Node/Image editor - rather than its
// 3D viewport's orbit).
void handleViewNavigation(editor::Document* doc) {
  if (!doc->getWorld()) {
    return;
  }

  if (!editor::mouseInteractingWithBackground()) {
    return;
  }

  auto const& io = ImGui::GetIO();

  auto panBy = [](ImVec2 const& screenDelta) {
    gViewOffset.x -= screenDelta.x / gViewZoom;
    gViewOffset.y += screenDelta.y / gViewZoom;
  };

  if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
    panBy(io.MouseDelta);
  } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
    panBy(io.MouseDelta);
  }

  if (io.MouseWheel != 0.0f) {
    // Keep the world point under the cursor fixed on screen: read it before
    // and after changing zoom, and fold the difference into the pan offset.
    auto worldPosBeforeZoom = editor::getMouseWorldPosition();

    constexpr float ED_VIEW_ZOOM_STEP = 1.15f;
    float factor = io.MouseWheel > 0.0f ? ED_VIEW_ZOOM_STEP : 1.0f / ED_VIEW_ZOOM_STEP;
    gViewZoom = clamp(gViewZoom * factor, ED_MIN_VIEW_ZOOM, ED_MAX_VIEW_ZOOM);

    auto worldPosAfterZoom = editor::getMouseWorldPosition();
    gViewOffset += worldPosBeforeZoom - worldPosAfterZoom;
  }
}

void handleWorldInteraction(
    editor::Document* doc,
    editor::PointerInput const& input) {
  // View navigation remains a presentation concern; authored-object drag
  // semantics are delegated to the ImGui-free interaction seam below.
  if (input.cursorInMiniMap) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (input.leftDragging) {
      gViewOffset += wp::Vector2(input.dragDelta.x, -input.dragDelta.y) *
                     MINIMAP_SCALE;
    }
  }

  gEditorInteraction.updateDrag(doc, gEditorSettings, input);
}

void clampViewToWorldBounds() {
  if (!editor::Document::instance()->isActive()) {
    return;
  }

  auto const& worldBounds = editor::Document::instance()->getWorld()->getExtents();
  wp::Vector2 minExtent, maxExtent;

  worldBounds.getExtents(minExtent, maxExtent);

  auto visibleHalf = gWorldViewSize / gViewZoom * 0.5f;

  auto loX = minExtent.x + visibleHalf.x, hiX = maxExtent.x - visibleHalf.x;
  auto loY = minExtent.y + visibleHalf.y, hiY = maxExtent.y - visibleHalf.y;

  auto centre = worldBounds.getCentre();
  gViewOffset.x = loX <= hiX ? clamp(gViewOffset.x, loX, hiX) : centre.x;
  gViewOffset.y = loY <= hiY ? clamp(gViewOffset.y, loY, hiY) : centre.y;
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
  LONGLONG globalTimeMicros{0};
  while (!done) {
    // Get elapsed time
    QueryPerformanceCounter(&EndingTime);
    ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
    ElapsedMicroseconds.QuadPart *= 1000000;
    ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

    StartingTime = EndingTime;

    auto updateTimeMicros = ElapsedMicroseconds.QuadPart;
    globalTimeMicros += updateTimeMicros;

    // Events
    done = processEvents(gWindow);

    // Logic
    if (editor::Document::instance()->isActive()) {
      auto doc = editor::Document::instance();

      auto const& proxyPos = doc->getPlayerProxyPosition();
      auto proxyAngle = doc->getPlayerProxyAngle();

      bool playerMoved = doc->getPlayerOldProxyPosition() != proxyPos;
      bool playerTurned = doc->getPlayerOldProxyAngle() != proxyAngle;

      doc->getWorld()->update(updateTimeMicros / 1000000.0f, {proxyPos, proxyAngle, BW_PLAYER_RADIUS, BW_PLAYER_FOV, BW_PLAYER_VIEW_DISTANCE, playerMoved, playerTurned, doc->getWorld()->getWorldDataGenerator()->getLayerSelection()}, {ED_WINDOW_WIDTH, ED_WINDOW_HEIGHT});
    }

    // Rendering
    glViewport(0, 0, ED_WINDOW_WIDTH, ED_WINDOW_HEIGHT);
    glClearColor(clearColour.x * clearColour.w, clearColour.y * clearColour.w, clearColour.z * clearColour.w, clearColour.w);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();

    auto doc = editor::Document::instance();
    auto mouseButtonStatus = getMouseButtonStatus();

    // Get world data
    bw::core::WorldDataPtr worldData;
    bw::core::WorldData const* worldDataPtr{nullptr};

    if (doc->isActive()) {
      worldData = doc->getWorld()->getWorldData();

      worldDataPtr = worldData.get();

      auto pointerInput = readPointerInput(doc, mouseButtonStatus);

      // The main loop only samples input and delegates editor decisions.
      if (!io.WantCaptureMouse) {
        handleSelections(doc, worldDataPtr, gEditorSettings, pointerInput);
      }

      handleWorldInteraction(doc, pointerInput);
      handleViewNavigation(doc);
    }

    clampViewToWorldBounds();

    double globalTime = globalTimeMicros / 1'000'000.0;
    editor::renderWidgets(doc, gEditorSettings, worldDataPtr, globalTime);

    if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
      showDemoWindow = !showDemoWindow;
    }

    if (showDemoWindow) {
      ImGui::SetNextWindowFocus();
      ImGui::ShowDemoWindow();
      // ImPlot::ShowDemoWindow();
    }

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
  // May run before setupLogging() has completed, so the logger can be absent.
  if (gLogger) {
    gLogger->critical(msg);
  }
  outputToDebugger(msg);
}

//
// Entrypoint
//
int main(int, char**) {
  int exitCode{0};

  try {
    // Inside the try: initialisation throws too, and left outside it an
    // uncaught exception here aborts with nothing written to the log.
    initialise();
    setup();
    run();
  } catch (ExitApplicationException& e) {
    exitCode = e.getExitCode();
    outputException(e.getMessage());
  } catch (EditorException& e) {
    exitCode = 1;
    outputException(e.getMessage());
  } catch (exception& e) {
    exitCode = 1;
    outputException(e.what());
  }

  shutdown();

  return exitCode;
}
