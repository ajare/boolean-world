#include "EditorRenderSystem.h"

#include <filesystem>
#include <stdexcept>

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/common/Logger.h>

using namespace std;

namespace editor {

EditorRenderSystem::EditorRenderSystem(int width, int height) {
  mMppLogger = new mpp::Logger();
  if (!mMppLogger->initialise("EditorRenderSystem-mpp.log", mpp::Logger::Level::Debug)) {
    delete mMppLogger;
    mMppLogger = nullptr;
    throw std::runtime_error("EditorRenderSystem: could not create mpp logger");
  }

  mLogger = new wp::Logger();
  mLogger->open("EditorRenderSystem.html");

  // Built against whichever GL context is already current - the editor's
  // own SDL3 window/context, created once in Main.cpp. No window/context of
  // its own.
  mRenderSystem = new mpp::RenderSystem((size_t)width, (size_t)height, mMppLogger);
  mRenderResourceMgr = new mpp::ResourceManager(mRenderSystem, mMppLogger);
  mRenderSystem->createCoreResources(mRenderResourceMgr);

  mResourceMgr = new wp::application::resourcesystem::ResourceManager(
      mRenderSystem, mRenderResourceMgr, nullptr /* no audio in the editor */, mLogger);

  mResourceMgr->addResourceLocationFactory(
      "Directory",
      [logger = mLogger](string const& location, string const& definitionFile) -> wp::application::resourcesystem::ResourceLocation* {
        return new wp::application::resourcesystem::DirectoryResourceLocation(logger, location, definitionFile);
      });
  // No "ZipFile" factory: BW_EDITOR_PROC_MATERIAL_MANIFEST is always a plain
  // directory on disk, and the only existing ZipResourceLocation is a
  // Launcher-private class (src/Launcher), not something this can link
  // against without duplicating its vendored miniz dependency for a location
  // type nothing here ever adds.

  filesystem::path manifest{BW_EDITOR_PROC_MATERIAL_MANIFEST};
  mResourceMgr->addResourceLocation(
      "Directory", manifest.parent_path().string(), manifest.filename().string());
}

EditorRenderSystem::~EditorRenderSystem() {
  delete mResourceMgr;
  mResourceMgr = nullptr;

  if (mRenderSystem) {
    mRenderSystem->destroyCoreResources();
  }

  delete mRenderResourceMgr;
  mRenderResourceMgr = nullptr;

  delete mRenderSystem;
  mRenderSystem = nullptr;

  delete mLogger;
  mLogger = nullptr;

  delete mMppLogger;
  mMppLogger = nullptr;
}

}  // namespace editor
