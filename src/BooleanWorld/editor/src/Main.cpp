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
SDL_GLContext gContext;

editor::Settings gEditorSettings;
editor::HoverableType gHoveredType{editor::HoverableType::None};
std::vector<uint32_t> gHoveredIndices;

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

  for (const auto& entry : filesystem::directory_iterator(dir)) {
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
  gLogger->info("Shutting down");

  delete gLogger;
  gLogger = nullptr;

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
  editor::MouseButtonStatus status;

  auto const& io = ImGui::GetIO();

  static ImVec2 frameDragDelta[2];

  if (!io.WantCaptureMouse) {
    for (int i = 0; i < 2; ++i) {
      if (io.MouseClicked[i]) {
        frameDragDelta[i] = ImGui::GetMouseDragDelta(i);
        status.state[i] = editor::MouseButtonStatus::State::Clicked;
      } else if (io.MouseDown[i]) {
        status.state[i] = editor::MouseButtonStatus::State::Down;
      } else if (io.MouseReleased[i]) {
        status.state[i] = editor::MouseButtonStatus::State::Released;
      }

      status.dragging[i] = ImGui::IsMouseDragging(i, 2);
      status.dragDelta[i] = ImGui::GetMouseDragDelta(i) - frameDragDelta[i];

      frameDragDelta[i] = ImGui::GetMouseDragDelta(i);
    }
  } else {
    for (int i = 0; i < 2; ++i) {
      status.state[i] = editor::MouseButtonStatus::State::Unavailable;
      status.dragging[i] = false;
      status.dragDelta[i] = {0, 0};
    }
  }

  return status;
}

// Rubber-band multi-select drag state. Read by handleWorldInteraction too, so
// that a background drag intended as a box select is not also interpreted as
// a move/scale/rotate of whatever's already selected.
bool gBoxSelectPending = false;
bool gBoxSelectDragging = false;

void handleSelections(editor::Document* doc, bw::core::WorldData const* worldData, editor::Settings const& settings) {
  static int curHoveredPrimitiveIndex = -1;

  auto hoveredPrimitiveIndices = editor::getHoveredPrimitiveIndices(doc, settings);
  auto hoveredTriggerLineIndex = editor::getHoveredTriggerLineIndex(doc, settings);
  auto hoveredWorldVertexIndex = editor::getHoveredWorldVertexIndex(doc, settings, worldData);

  // Choose primary hovered object
  if (hoveredWorldVertexIndex != ~0u) {
    gHoveredType = editor::HoverableType::WorldVertex;
    gHoveredIndices = {hoveredWorldVertexIndex};
  } else if (hoveredTriggerLineIndex != ~0u) {
    gHoveredType = editor::HoverableType::TriggerLine;
    gHoveredIndices = {hoveredTriggerLineIndex};
  } else if (!hoveredPrimitiveIndices.empty()) {
    gHoveredType = editor::HoverableType::Primitive;
    gHoveredIndices = hoveredPrimitiveIndices;
  } else {
    gHoveredType = editor::HoverableType::None;
    gHoveredIndices.clear();
  }

  auto const& selectedPrimitiveIndices = doc->getSelectedPrimitiveIndices();
  auto const& io = ImGui::GetIO();

  // Shift+Alt+left-mouse-button is reserved for panning the view (see
  // handleViewNavigation), so it must not also select what's under it.
  if (io.KeyShift && io.KeyAlt) {
    if (gHoveredType != editor::HoverableType::None) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    return;
  }

  // Rubber-band multi-select: a left-mouse-button drag started over empty
  // background draws a rectangle, and on release selects (or, with
  // Ctrl/Shift held, toggles/adds) whatever primitives it overlaps. A click
  // that never crosses the drag threshold falls through to the plain
  // click-to-deselect behaviour below.
  static ImVec2 sBoxSelectStartScreen;
  constexpr float ED_BOX_SELECT_DRAG_THRESHOLD_SQ = 4.0f;

  // Select single primitive on left-mouse click
  if (ImGui::IsMouseClicked(0)) {
    switch (gHoveredType) {
      case editor::HoverableType::Primitive:
        // Multiple Primitives stacked under the cursor need repeated plain
        // clicks to cycle through, which relies on this first click of a
        // pair leaving a click-through in progress alone (see the release
        // branch below). That ambiguity does not exist with only one
        // Primitive hovered, so a plain click there always takes effect
        // immediately, collapsing any other selection down to just it - the
        // same as clicking a Primitive that was not selected at all.
        if (hoveredPrimitiveIndices.size() == 1 || !doc->anyPrimitiveIndicesSelected(hoveredPrimitiveIndices)) {
          // The cycle restarts whenever what is under the cursor changes, so
          // a first click lands on the head of the list - the ghost, when it
          // is one of them - rather than wherever the last cycle left off.
          static vector<uint32_t> cycledPrimitiveIndices;

          if (cycledPrimitiveIndices != hoveredPrimitiveIndices) {
            cycledPrimitiveIndices = hoveredPrimitiveIndices;
            curHoveredPrimitiveIndex = -1;
          }

          curHoveredPrimitiveIndex = (curHoveredPrimitiveIndex + 1) % hoveredPrimitiveIndices.size();
          auto hoveredPrimitiveIndex = hoveredPrimitiveIndices[curHoveredPrimitiveIndex];

          if (io.KeyCtrl) {
            auto f = bind(editor::togglePrimitiveSelected, placeholders::_1, hoveredPrimitiveIndex);
            editor::transactUndoableAction(doc, format("Toggle Primitive {}", hoveredPrimitiveIndex), f);
          } else if (io.KeyShift) {
            if (selectedPrimitiveIndices.find(hoveredPrimitiveIndex) == selectedPrimitiveIndices.end()) {
              auto f = bind(editor::addPrimitivesToSelection, placeholders::_1, set<uint32_t>{hoveredPrimitiveIndex});
              editor::transactUndoableAction(doc, format("Add Primitive {} To Selection", hoveredPrimitiveIndex), f);
            }
          } else {
            // A plain click always ends with exactly this Primitive selected
            // - not merely "among the selection", which a multi-selection
            // (e.g. from a rubber-band drag) could already satisfy without
            // this being the only Primitive selected.
            if (selectedPrimitiveIndices != set<uint32_t>{hoveredPrimitiveIndex}) {
              auto f = bind(editor::selectPrimitive, placeholders::_1, hoveredPrimitiveIndex);
              editor::transactUndoableAction(doc, format("Select Primitive {}", hoveredPrimitiveIndex), f);
            }
          }
        }
        break;

      case editor::HoverableType::TriggerLine:
        editor::transactUndoableAction(doc, format("Select TriggerLine {}", hoveredTriggerLineIndex),
                                       bind(editor::selectTriggerLine, placeholders::_1, hoveredTriggerLineIndex));
        break;

      case editor::HoverableType::WorldVertex:
        editor::transactUndoableAction(doc, format("Select World Vertex {}", hoveredWorldVertexIndex),
                                       bind(editor::selectWorldVertex, placeholders::_1, hoveredWorldVertexIndex));
        break;

      case editor::HoverableType::None: {
        // The minimap has its own drag-to-pan handling in
        // handleWorldInteraction; don't also arm a box select there.
        auto miniMapBounds = getMiniMapBounds(doc);
        miniMapBounds.setPosition(miniMapBounds.getMinExtent() + gWorldViewScreenOrigin);
        auto mouseScreenPos = ImGui::GetMousePos();

        if (!doc->getWorld() || !settings.renderMiniMap ||
            !miniMapBounds.pointInside(mouseScreenPos.x, mouseScreenPos.y)) {
          gBoxSelectPending = true;
          gBoxSelectDragging = false;
          sBoxSelectStartScreen = mouseScreenPos;
        }
        break;
      }

      default:
        break;
    }
  }

  if (gBoxSelectPending && ImGui::IsMouseDown(0) && !ImGui::IsMouseClicked(0)) {
    auto currentScreen = ImGui::GetMousePos();
    auto delta = ImVec2(currentScreen.x - sBoxSelectStartScreen.x, currentScreen.y - sBoxSelectStartScreen.y);

    if (!gBoxSelectDragging && (delta.x * delta.x + delta.y * delta.y) > ED_BOX_SELECT_DRAG_THRESHOLD_SQ) {
      gBoxSelectDragging = true;
    }

    if (gBoxSelectDragging) {
      ImVec2 rectMin{min(sBoxSelectStartScreen.x, currentScreen.x), min(sBoxSelectStartScreen.y, currentScreen.y)};
      ImVec2 rectMax{max(sBoxSelectStartScreen.x, currentScreen.x), max(sBoxSelectStartScreen.y, currentScreen.y)};

      auto drawList = ImGui::GetForegroundDrawList();
      drawList->AddRectFilled(rectMin, rectMax, IM_COL32(180, 200, 255, 40));
      drawList->AddRect(rectMin, rectMax, IM_COL32(180, 200, 255, 220));
    }
  }

  if (ImGui::IsMouseReleased(0)) {
    if (gBoxSelectPending) {
      gBoxSelectPending = false;

      if (gBoxSelectDragging) {
        gBoxSelectDragging = false;

        auto startWorld = editor::screenToWorldPosition(sBoxSelectStartScreen);
        auto endWorld = editor::getMouseWorldPosition();

        wp::Vector2 minExtent{min(startWorld.x, endWorld.x), min(startWorld.y, endWorld.y)};
        wp::Vector2 maxExtent{max(startWorld.x, endWorld.x), max(startWorld.y, endWorld.y)};
        wp::BoundingBox boxBounds(minExtent, maxExtent - minExtent);

        auto boxIndices = doc->getPrimitiveIndicesInBounds(boxBounds, settings);
        set<uint32_t> boxIndicesSet(boxIndices.begin(), boxIndices.end());

        if (!boxIndicesSet.empty()) {
          if (io.KeyCtrl) {
            auto f = bind(editor::togglePrimitivesSelected, placeholders::_1, boxIndicesSet);
            editor::transactUndoableAction(doc, "Toggle Primitives In Selection Box", f);
          } else if (io.KeyShift) {
            auto f = bind(editor::addPrimitivesToSelection, placeholders::_1, boxIndicesSet);
            editor::transactUndoableAction(doc, "Add Primitives In Selection Box", f);
          } else {
            auto f = bind(editor::selectPrimitives, placeholders::_1, boxIndicesSet);
            editor::transactUndoableAction(doc, "Select Primitives In Selection Box", f);
          }
        } else if (!io.KeyCtrl && !io.KeyShift) {
          editor::transactUndoableAction(doc, "Clear selection", editor::clearSelections);
        }
      } else if (!io.KeyCtrl && !io.KeyShift) {
        editor::transactUndoableAction(doc, "Clear selection", editor::clearSelections);
      }
    } else {
      uint32_t hoveredIndex;

      switch (gHoveredType) {
        case editor::HoverableType::Primitive:
          curHoveredPrimitiveIndex = (curHoveredPrimitiveIndex + 1) % hoveredPrimitiveIndices.size();
          hoveredIndex = hoveredPrimitiveIndices[curHoveredPrimitiveIndex];

          if (io.KeyCtrl) {
            auto f = bind(editor::togglePrimitiveSelected, placeholders::_1, hoveredIndex);
            editor::transactUndoableAction(doc, format("Toggle Primitive {}", hoveredIndex), f);
          } else if (io.KeyShift) {
            if (selectedPrimitiveIndices.find(hoveredIndex) == selectedPrimitiveIndices.end()) {
              auto f = bind(editor::addPrimitivesToSelection, placeholders::_1, set<uint32_t>{hoveredIndex});
              editor::transactUndoableAction(doc, format("Add Primitive {} To Selection", hoveredIndex), f);
            }
          } else {
            // See the matching comment in the click branch above: a plain
            // click always leaves exactly this Primitive selected.
            if (selectedPrimitiveIndices != set<uint32_t>{hoveredIndex}) {
              auto f = bind(editor::selectPrimitive, placeholders::_1, hoveredIndex);
              editor::transactUndoableAction(doc, format("Select Primitive {}", hoveredIndex), f);
            }
          }
          break;

        default:
          break;
      }
    }
  }

  if (gHoveredType != editor::HoverableType::None) {
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

void handleWorldInteraction(editor::Document* doc, bw::core::WorldData const* worldData, editor::MouseButtonStatus const& mouseStatus, editor::Settings const& settings) {
  // Check Minimap
  if (doc->getWorld()) {
    auto mouseScreenPos = ImGui::GetMousePos();
    auto miniMapBounds = getMiniMapBounds(doc);
    miniMapBounds.setPosition(miniMapBounds.getMinExtent() + gWorldViewScreenOrigin);

    if (settings.renderMiniMap && miniMapBounds.pointInside(mouseScreenPos.x, mouseScreenPos.y)) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

      if (mouseStatus.dragging[editor::MouseButtonStatus::Left]) {
        auto delta = mouseStatus.dragDelta[editor::MouseButtonStatus::Left];
        gViewOffset += wp::Vector2(delta.x, -delta.y) * MINIMAP_SCALE;
        return;
      }
    }
  }

  // Shift+Alt+left-mouse-button drag is reserved for panning the view (see
  // handleViewNavigation), so it must not also transform the selection.
  if (ImGui::GetIO().KeyShift && ImGui::GetIO().KeyAlt) {
    return;
  }

  // A left-mouse-button drag that started over empty background is a
  // rubber-band selection (see handleSelections), not a grab of whatever is
  // already selected.
  if (gBoxSelectPending) {
    return;
  }

  // Check selections
  static bool movingSelectedPrimitives{false};
  static bool scalingSelectedPrimitives{false};
  static bool rotatingSelectedPrimitives{false};
  static bool movingSelectedTriggerArea{false};
  static int movingSelectedTriggerAreaPart{-1};

  auto const& primitiveSelection = doc->getSelectedPrimitiveIndices();
  auto selectedTriggerLineIndex = doc->getSelectedTriggerLineIndex();

  if (primitiveSelection.empty() && selectedTriggerLineIndex == ~0u) {
    return;
  }

  // Primitives
  if (!primitiveSelection.empty()) {
    if (mouseStatus.state[editor::MouseButtonStatus::Left] == editor::MouseButtonStatus::State::Released) {
      if ((movingSelectedPrimitives || scalingSelectedPrimitives || rotatingSelectedPrimitives) &&
          editor::undoableActionInProgress()) {
        editor::commitUndoableAction(doc);
      }

      movingSelectedPrimitives = false;
      scalingSelectedPrimitives = false;
      rotatingSelectedPrimitives = false;
    }

    if (mouseStatus.dragging[editor::MouseButtonStatus::Left]) {
      if (ImGui::GetIO().KeyShift) {
        if (!scalingSelectedPrimitives) {
          scalingSelectedPrimitives = true;
          if (!editor::undoableActionInProgress()) {
            editor::beginUndoableAction(doc, "Transform Primitive(s)", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
          }
        }

        // Scale selected primitives by mouse Y
        for (auto index : primitiveSelection) {
          auto primitive = doc->getWorld()->getPrimitive(index);
          auto delta = -mouseStatus.dragDelta[editor::MouseButtonStatus::Left].y * 0.01f;
          auto const key = bw::core::VertexTransformer::Key::Scale;
          auto newScale = primitive->getAnimationInterpolator(key).getValue(0.0f) + delta;

          // If just 2 values, set them both so we have a contant angle.  Otherwise just set the first frame
          {
            auto mutation = primitive->mutate();
            auto& animation = mutation.animation(key);
            animation.updatePoint(0, 0.0f, newScale);

            if (animation.getNumPoints() == 2) {
              animation.updatePoint(1, 1.0f, newScale);
            }
          }

          // Update vertices for visual purposes
          primitive->updateVertexPositions();
        }
      } else if (scalingSelectedPrimitives) {
        // We've stopped scaling
        scalingSelectedPrimitives = false;
      }

      if (ImGui::GetIO().KeyAlt) {
        if (!rotatingSelectedPrimitives) {
          rotatingSelectedPrimitives = true;
          if (!editor::undoableActionInProgress()) {
            editor::beginUndoableAction(doc, "Transform Primitive(s)", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
          }
        }

        // Rotate selected primitives by mouse X
        for (auto index : primitiveSelection) {
          auto primitive = doc->getWorld()->getPrimitive(index);
          auto delta = mouseStatus.dragDelta[editor::MouseButtonStatus::Left].x;
          auto const key = bw::core::VertexTransformer::Key::Angle;
          auto newAngle = primitive->getAnimationInterpolator(key).getValue(0.0f) + delta;

          // If just 2 values, set them both so we have a contant angle.  Otherwise just set the first frame
          {
            auto mutation = primitive->mutate();
            auto& animation = mutation.animation(key);
            animation.updatePoint(0, 0.0f, newAngle);

            if (animation.getNumPoints() == 2) {
              animation.updatePoint(1, 1.0f, newAngle);
            }
          }

          // Update vertices for visual purposes
          primitive->updateVertexPositions();
        }
      } else if (rotatingSelectedPrimitives) {
        // We've stopped scaling
        rotatingSelectedPrimitives = false;
      }

      if (!scalingSelectedPrimitives && !rotatingSelectedPrimitives) {
        if (!movingSelectedPrimitives) {
          movingSelectedPrimitives = true;
          if (!editor::undoableActionInProgress()) {
            editor::beginUndoableAction(doc, "Transform Primitive(s)", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
          }
        }

        // Move selected primitives to mouse position
        for (auto index : primitiveSelection) {
          auto primitive = doc->getWorld()->getPrimitive(index);

          auto delta = mouseStatus.dragDelta[editor::MouseButtonStatus::Left];
          auto newPos = primitive->getPosition() + wp::Vector2{delta.x, -delta.y};
          primitive->setPosition(newPos);

          // Update vertices for visual purposes
          primitive->updateVertexPositions();
        }
      }
    }
  }

  // Trigger line
  if (selectedTriggerLineIndex != ~0u) {
    if (mouseStatus.state[editor::MouseButtonStatus::Left] == editor::MouseButtonStatus::State::Released) {
      if (movingSelectedTriggerArea && editor::undoableActionInProgress()) {
        editor::commitUndoableAction(doc);
      }

      movingSelectedTriggerArea = false;
      movingSelectedTriggerAreaPart = -1;
    }

    if (mouseStatus.dragging[editor::MouseButtonStatus::Left]) {
      if (!movingSelectedTriggerArea) {
        movingSelectedTriggerArea = true;
        if (!editor::undoableActionInProgress()) {
          editor::beginUndoableAction(doc, "Move TriggerLine", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
        }
      }

      // Find the part of the trigger which we're moving - handle or line
      auto world = doc->getWorld();
      auto triggerLine = world->getTriggerLine(selectedTriggerLineIndex);

      auto p0 = triggerLine->getPoint(0);
      auto p1 = triggerLine->getPoint(1);

      auto mouseWorldPos = editor::getMouseWorldPosition();
      auto delta = mouseStatus.dragDelta[editor::MouseButtonStatus::Left];
      auto triggerLineHandleRadiusSq = settings.triggerLineHandleRadius * settings.triggerLineHandleRadius;

      auto movement = wp::Vector2{delta.x, -delta.y};
      if (mouseWorldPos.distanceToSq(p0) <= triggerLineHandleRadiusSq || movingSelectedTriggerAreaPart == 0) {
        world->setTriggerLinePoint(selectedTriggerLineIndex, 0, p0 + movement);
        movingSelectedTriggerAreaPart = 0;
      } else if (mouseWorldPos.distanceToSq(p1) <= triggerLineHandleRadiusSq || movingSelectedTriggerAreaPart == 1) {
        world->setTriggerLinePoint(selectedTriggerLineIndex, 1, p1 + movement);
        movingSelectedTriggerAreaPart = 1;
      } else {
        world->moveTriggerLine(selectedTriggerLineIndex, movement);
        movingSelectedTriggerAreaPart = 2;
      }
    }
  }
}

void handleContinuousKeyboardInput(uint64_t updateTimeMicros) {
  const float MoveSpeed{500.0f};

  auto const& io = ImGui::GetIO();

  if (io.WantCaptureKeyboard) {
    return;
  }

  if (!editor::Document::instance()->isActive()) {
    return;
  }

  float frameTime = updateTimeMicros / 1000000.0f;
  float moveSpeed = MoveSpeed * frameTime * (io.KeyShift ? 4.0f : 1.0f);

  // Clamp to world bounds
  auto const& worldBounds = editor::Document::instance()->getWorld()->getExtents();
  wp::Vector2 minExtent, maxExtent;

  worldBounds.getExtents(minExtent, maxExtent);

  gViewOffset.x = clamp(gViewOffset.x, minExtent.x + gWorldViewSize.x * 0.5f, maxExtent.x - gWorldViewSize.x * 0.5f);
  gViewOffset.y = clamp(gViewOffset.y, minExtent.y + gWorldViewSize.y * 0.5f, maxExtent.y - gWorldViewSize.y * 0.5f);
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

      // Set up selections, eg for logic on them
      if (!io.WantCaptureMouse) {
        handleSelections(doc, worldDataPtr, gEditorSettings);
      }

      handleWorldInteraction(doc, worldDataPtr, mouseButtonStatus, gEditorSettings);
      handleViewNavigation(doc);
    }

    handleContinuousKeyboardInput(updateTimeMicros);

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
