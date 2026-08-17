#define WINDOW_GLFW 1
#define WINDOW_SDL 2

#define WINDOWING_SYSTEM WINDOW_GLFW

#include <format>
#include <iostream>

#include "utils/StringUtils.h"

#include "Platform.h"

#if APP_PLATFORM == APP_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include <willpower/common/Exceptions.h>
#include <willpower/common/Logger.h>
#include <willpower/common/Timer.h>

#include <willpower/application/ServiceLocator.h>
#include <willpower/application/AudioSystem.h>
#include <willpower/application/ApplicationSettings.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include <mpp/MppException.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/ProgrammaticTextureStream.h>
#include <mpp/Logger.h>
#include <mpp/BufferRenderer.h>

#if WINDOWING_SYSTEM == WINDOW_SDL
#include <SDL3/SDL_main.h>
#endif

#include "imgui/imgui.h"
#include "imgui/implot.h"

#include "ProgramOptions.h"
#include "ApplicationDLL.h"
#include "LauncherLifecycle.h"
#include "StateManager.h"
#include "ExitApplicationException.h"
#include "DirectoryResourceLocation.h"
#include "ZipResourceLocation.h"
#include "ImGuiDataProvider.h"

#if WINDOWING_SYSTEM == WINDOW_SDL
#include "sdl/WindowSDL.h"
#include "sdl/TimerSDL.h"
#elif WINDOWING_SYSTEM == WINDOW_GLFW
#include "glfw/WindowGLFW.h"
#include "glfw/TimerGLFW.h"
#include "glfw/ImGuiGLFW.h"
#endif

using namespace std;
using namespace wp;

// Logging
Logger* gLogger = nullptr;
mpp::Logger* gMppLogger = nullptr;

// Platform objects
static ApplicationDLL* gDLL = nullptr;
static StateManager* gStateMgr = nullptr;
static wp::application::AudioSystem* gAudioSystem = nullptr;

#if WINDOWING_SYSTEM == WINDOW_SDL
static WindowSDL* gWindow = nullptr;
static TimerSDL* gTimer = nullptr;
#elif WINDOWING_SYSTEM == WINDOW_GLFW
static WindowGLFW* gWindow = nullptr;
static TimerGLFW* gTimer = nullptr;
#endif

// Application objects
static application::ApplicationSettings* gAppSettings = nullptr;
static application::resourcesystem::ResourceManager* gResourceMgr = nullptr;

bool gDisplayDebugEnabled = false;

// Rendering objects
static mpp::RenderSystem* gRenderSystem = nullptr;
static mpp::ResourceManager* gRenderSystemResourceMgr = nullptr;
static shared_ptr<ImGuiDataProvider> gImGuiDataProvider;
static mpp::BufferRenderer* gImGuiRenderer = nullptr;

void initialiseImGui(float contentScale) {
  ImGuiIO& io = ImGui::GetIO();

  // Configure ImGui
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // TODO: Set optional io.ConfigFlags values, e.g. 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard' to enable keyboard controls.
  // TODO: Fill optional fields of the io structure later.
  // TODO: Load TTF/OTF fonts if you don't want to use the default font.
  ImFontConfig fontCfg;
  fontCfg.SizePixels = 13.0f * contentScale;

  io.Fonts->AddFontDefault(&fontCfg);

  auto fontRes = gRenderSystemResourceMgr->getResource("__ImGui_Font__", true);
  if (!fontRes) {
    int fontWidth, fontHeight;
    unsigned char* fontData{nullptr};

    io.Fonts->GetTexDataAsRGBA32(&fontData, &fontWidth, &fontHeight);

    auto fontTextureStr = new mpp::ProgrammaticTextureStream(gRenderSystemResourceMgr);

    fontTextureStr->setTarget(mpp::TextureTarget::Texture2D);
    fontTextureStr->setData([fontData, fontWidth, fontHeight](string const&) {
      mpp::TextureData data;

      data.width = fontWidth;
      data.height = fontHeight;
      data.bitsPerPixel = 32;
      data.dataType = GL_UNSIGNED_BYTE;
      data.pixelFormat = GL_RGBA;

      size_t dataSize = (data.width * data.height * data.bitsPerPixel / 8);

      data.data = new uint8_t[dataSize];
      memcpy(data.data, fontData, dataSize);

      return data;
    });

    fontTextureStr->setFiltering(mpp::TextureParams::MinFilter::Linear, mpp::TextureParams::MagFilter::Linear);

    fontRes = gRenderSystemResourceMgr->declareResource("__ImGui_Font__", mpp::ResourceStreamPtr(fontTextureStr)).first;
    fontRes->load();
  }

  io.Fonts->SetTexID((ImTextureID)(intptr_t)fontRes->getId());

  io.DisplaySize.x = (float)gRenderSystem->getWindowWidth();
  io.DisplaySize.y = (float)gRenderSystem->getWindowHeight();

  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(contentScale);
}

//
// Initialise all systems
//
ProgramOptions startup(string const& configFile, LauncherLifecycle& lifecycle) {
  using Service = LauncherLifecycle::Service;

  // Create loggers
  gLogger = new Logger();
  lifecycle.track(Service::Logger, []() {
    delete gLogger;
    gLogger = nullptr;
  });
  gLogger->open("LauncherLog.html");

  gMppLogger = new mpp::Logger();
  lifecycle.track(Service::MppLogger, []() {
    delete gMppLogger;
    gMppLogger = nullptr;
  });
  if (!gMppLogger->initialise("mpp.log", mpp::Logger::Level::Debug)) {
    throw exception("Could not create MPP logger!");
  }

  // Read in program options
  ProgramOptions options = parseProgramOptions(configFile);

  logProgramOptions(options, gLogger);

  // Set up application settings
  gAppSettings = new application::ApplicationSettings();
  gAppSettings->VideoWidth = options.screenWidth;
  gAppSettings->VideoHeight = options.screenHeight;
  gAppSettings->Fullscreen = options.fullScreen;

  application::ServiceLocator::provideApplicatonSettings(gAppSettings);
  lifecycle.track(Service::ApplicationSettings, []() {
    application::ServiceLocator::provideApplicatonSettings(nullptr);
    delete gAppSettings;
    gAppSettings = nullptr;
  });

#if WINDOWING_SYSTEM == WINDOW_SDL
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw exception("Could not initialise SDL subsystem!");
  }
  lifecycle.track(Service::Platform, []() { SDL_Quit(); });

  // Create timer
  gTimer = new TimerSDL();
  lifecycle.track(Service::Timer, []() {
    delete gTimer;
    gTimer = nullptr;
  });

  // Create window
  gWindow = new WindowSDL("Window", options);
  lifecycle.track(Service::Window, []() {
    delete gWindow;
    gWindow = nullptr;
  });
  gWindow->create();
#elif WINDOWING_SYSTEM == WINDOW_GLFW
  if (!glfwInit()) {
    throw exception("Could not initialise GLFW subsystem!");
  }
  lifecycle.track(Service::Platform, []() { glfwTerminate(); });

  // Create timer
  gTimer = new TimerGLFW();
  lifecycle.track(Service::Timer, []() {
    delete gTimer;
    gTimer = nullptr;
  });

  // Create window
  gWindow = new WindowGLFW("Window", options);
  lifecycle.track(Service::Window, []() {
    delete gWindow;
    gWindow = nullptr;
  });
  gWindow->create();
#endif

  // Create render system
  // mpp::enable_static_log(MPP_RESOURCE_LOGFILE, true);

  gRenderSystem = new mpp::RenderSystem(gWindow->getWidth(), gWindow->getHeight(), gMppLogger);
  lifecycle.track(Service::RenderSystem, []() {
    delete gRenderSystem;
    gRenderSystem = nullptr;
  });
  gRenderSystemResourceMgr = new mpp::ResourceManager(gRenderSystem, gMppLogger);
  lifecycle.track(Service::RenderResourceManager, []() {
    gRenderSystemResourceMgr->dumpResources("final-resources.csv");
    delete gRenderSystemResourceMgr;
    gRenderSystemResourceMgr = nullptr;
  });
  lifecycle.track(Service::RenderCoreResources, []() {
    if (gRenderSystem) {
      gRenderSystem->destroyCoreResources();
    }
  });
  gRenderSystem->createCoreResources(gRenderSystemResourceMgr);

  // Audio
  gAudioSystem = options.audioEnabled ? new wp::application::AudioSystem(options.audio) : nullptr;
  if (gAudioSystem) {
    lifecycle.track(Service::AudioSystem, []() {
      delete gAudioSystem;
      gAudioSystem = nullptr;
    });
  }

  // Resource manager
  gResourceMgr = new application::resourcesystem::ResourceManager(gRenderSystem, gRenderSystemResourceMgr, gAudioSystem, gLogger);
  lifecycle.track(Service::ResourceManager, []() {
    delete gResourceMgr;
    gResourceMgr = nullptr;
  });

  // Add resource location factories
  gResourceMgr->addResourceLocationFactory("Directory", [](string const& location, string const& definitionFile) -> application::resourcesystem::ResourceLocation* {
    return new DirectoryResourceLocation(gLogger, location, definitionFile);
  });

  gResourceMgr->addResourceLocationFactory("ZipFile", [](string const& location, string const& definitionFile) -> application::resourcesystem::ResourceLocation* {
    return new ZipResourceLocation(gLogger, location, definitionFile);
  });

  // Add resource locations
  for (auto const& rl : options.resourceLocations) {
    gResourceMgr->addResourceLocation(rl.type, rl.path, rl.definitionFile);
  }

  // ImGui. Track each context as soon as it exists so failures later in
  // initialisation still unwind only the portions that were constructed.
  auto imGuiContext = ImGui::CreateContext();
  lifecycle.track(Service::ImGuiContext, [imGuiContext]() { ImGui::DestroyContext(imGuiContext); });
  auto imPlotContext = ImPlot::CreateContext();
  lifecycle.track(Service::ImPlotContext, [imPlotContext]() { ImPlot::DestroyContext(imPlotContext); });
#if WINDOWING_SYSTEM == WINDOW_GLFW
  initialiseImGuiForGlfw(gWindow->getWindow());
  lifecycle.track(Service::ImGuiBackend, []() { shutdownImGuiForGlfw(); });
#endif
  initialiseImGui(gWindow->getContentScale());

  vector<mpp::ResourcePtr> imGuiTextures;
  imGuiTextures.push_back(gRenderSystemResourceMgr->getResource("__ImGui_Font__"));

  gImGuiDataProvider = make_shared<ImGuiDataProvider>(imGuiTextures);
  lifecycle.track(Service::ImGuiDataProvider, []() { gImGuiDataProvider.reset(); });
  gImGuiRenderer = new mpp::BufferRenderer(gImGuiDataProvider);
  lifecycle.track(Service::ImGuiRenderer, []() {
    delete gImGuiRenderer;
    gImGuiRenderer = nullptr;
  });

  // Load application DLL
  gDLL = new ApplicationDLL();
  lifecycle.track(Service::ApplicationDll, []() {
    delete gDLL;
    gDLL = nullptr;
  });
  gDLL->load(options.dll, options.arguments, gLogger, gResourceMgr);

  // Create state manager and get state factories
  gStateMgr = new StateManager(gResourceMgr, gAudioSystem, gRenderSystem, gRenderSystemResourceMgr);
  lifecycle.track(Service::StateManager, []() {
#if WINDOWING_SYSTEM == WINDOW_GLFW
    if (gWindow && gWindow->getWindow()) {
      gWindow->setStateManager(nullptr);
    }
#endif
    delete gStateMgr;
    gStateMgr = nullptr;
  });
  gDLL->registerStateFactories(gStateMgr);

#if WINDOWING_SYSTEM == WINDOW_GLFW
  gWindow->setStateManager(gStateMgr);
#endif

  return options;
}

//
// Helper to set up debug text to display
//

void setupDebugPanel() {
  string fpsColour;
  float fps = gTimer->getFPS();
  if (fps < 30) {
    fpsColour = "[#FF0000FF]";
  } else if (fps < 55) {
    fpsColour = "[#FFFF00FF]";
  } else {
    fpsColour = "[#00FF00FF]";
  }

  string fpsDisplay = std::format("FPS: {}{}", fpsColour, (int)fps);
  gRenderSystem->setDebugPreMessages({fpsDisplay});

  gRenderSystem->setDebugPostMessages(gStateMgr->getDebuggingText());

  gRenderSystem->showDebugPanel(gDisplayDebugEnabled,
                                mpp::RenderSystem::TimeUnit::Milliseconds,
                                mpp::RenderSystem::SizeUnit::Megabytes);
}

void updateImGui(float frameTime) {
  if (gStateMgr->imGuiActive()) {
    gWindow->showCursor(gStateMgr->imGuiCapturesInput());

    imGuiNewFrame();

    ImGuiIO& io = ImGui::GetIO();

    io.DeltaTime = frameTime;

    ImGui::NewFrame();

    // Pass the contexts across the DLL boundary
    auto imGuiCtx = ImGui::GetCurrentContext();
    auto imPlotCtx = ImPlot::GetCurrentContext();

    ImGuiMemAllocFunc imGuiAllocFunc;
    ImGuiMemFreeFunc imGuiFreeFunc;
    void* imGuiUserData;

    ImGui::GetAllocatorFunctions(&imGuiAllocFunc, &imGuiFreeFunc, &imGuiUserData);

    gStateMgr->renderImGui(frameTime, imGuiCtx, imPlotCtx, imGuiAllocFunc, imGuiFreeFunc, imGuiUserData);

    ImGui::EndFrame();
    ImGui::Render();

    gImGuiDataProvider->setDrawData(ImGui::GetDrawData());
  } else {
    gWindow->showCursor(false);
  }
}

//
// Entry point
//
#if WINDOWING_SYSTEM == WINDOW_SDL
int main(int argc, char** argv) {
  string configFile = "BooleanWorld.yaml";
  if (argc > 1) {
    configFile = string(argv[1]);
  }
#elif WINDOWING_SYSTEM == WINDOW_GLFW
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  string configFile = "BooleanWorld.yaml";
  if (__argc > 1) {
    configFile = string(__argv[1]);
  }
#endif

  LauncherLifecycle lifecycle;
  int exitCode = 0;
  uint64_t numFramesProcessed{0};
  double totalTime{0};
  int64_t totalTimeNs{0};
  try {
    auto options = startup(configFile, lifecycle);

    // Main loop
    float accum = 0.0f;
    const float updateFreq = 1.0f / 60.0f;

    mpp::RenderInfo renderInfo;

    gStateMgr->enterInitialState();
    gTimer->reset();

#if WINDOWING_SYSTEM == WINDOW_SDL
    while (true)
#elif WINDOWING_SYSTEM == WINDOW_GLFW
    while (gWindow->isActive())
#endif
    {
      // Get frame time
      float frameTime = gTimer->getDeltaTime();
      gTimer->addFrameToCounter(frameTime);

      accum += frameTime;

      // Process window messages
      gWindow->processEvents(gStateMgr);

      // Update current state
      while (accum >= updateFreq) {
        accum -= updateFreq;

        updateImGui(updateFreq);

        auto startTime = glfwGetTime();

        wp::Timer timerNs;
        auto startTimeNs = timerNs.elapsedNanoseconds();

        gStateMgr->update(updateFreq);

        if (gAudioSystem) {
          gAudioSystem->update();
        }

        auto endTime = glfwGetTime();
        auto endTimeNs = timerNs.elapsedNanoseconds();

        numFramesProcessed++;

        totalTime += (endTime - startTime);
        totalTimeNs += (endTimeNs - startTimeNs);
      }

      // Render
      setupDebugPanel();

      gRenderSystem->startStatsCollection();

      gStateMgr->render(gRenderSystem, gRenderSystemResourceMgr);

      auto ri = gRenderSystem->finishStatsCollection();

      if (gStateMgr->imGuiActive()) {
        gImGuiRenderer->render(gRenderSystem);
      }

      // Flip to screen
      gWindow->show();
    }
  } catch (ExitApplicationException& e) {
    if (auto average = LauncherLifecycle::averageDuration(totalTime, numFramesProcessed)) {
      gLogger->info(format("Avg update time ms: {}", *average * 1000.0));
      auto averageNs = LauncherLifecycle::averageDuration(static_cast<double>(totalTimeNs), numFramesProcessed);
      gLogger->info(format("Avg update time ms: {}", *averageNs / 1000000.0));
    } else {
      gLogger->info("No update frames processed.");
    }

    gLogger->info(e.getMessage());
    exitCode = e.getExitCode();
  } catch (application::resourcesystem::ResourceException& e) {
    if (auto const* resource = e.getResource()) {
      gLogger->error("Error in resource: " + resource->getQualifiedName());
    }
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  } catch (application::resourcesystem::ResourceSystemException& e) {
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  } catch (Exception& e) {
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  } catch (mpp::MppException& e) {
    // what() alone is useless here - MppGlException's message is just the bare
    // error name, e.g. "GL_INVALID_ENUM". The failing statement's location is
    // carried separately by the exception (GL_CHECK passes __LINE__/__FILE__/
    // __func__), so log that too or there is no way to find the call.
    auto detail = format("{} at {}:{} in {}\nstack trace: {}", e.what(), e.getFile(), e.getLine(),
                         e.getFunction(), e.getStackTrace());

    gLogger->error(detail);
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = detail.c_str();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  } catch (exception& e) {
    gLogger->error(e.what());
    exitCode = 1;

#ifdef _DEBUG
    char const* msg = e.what();

    size_t reqLength = ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), 0, 0);
    wstring ret(reqLength, L'\0');

    ::MultiByteToWideChar(CP_UTF8, 0, msg, (int)strlen(msg), &ret[0], (int)ret.length());
    OutputDebugString(ret.c_str());
#endif
  }

  lifecycle.teardown([](string_view service, exception_ptr error) {
    try {
      rethrow_exception(error);
    } catch (exception const& e) {
      if (gLogger) {
        gLogger->error(format("Error tearing down {}: {}", service, e.what()));
      }
    } catch (...) {
      if (gLogger) {
        gLogger->error(format("Unknown error tearing down {}", service));
      }
    }
  });
  return exitCode;
}