#include "EditorRenderSystem.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/common/Logger.h>

#include "ProcMaterial.h"
#include "ProcMaterialResourceDefinitionFactory.h"

using namespace std;

namespace editor {
namespace {

// Instantiates a plain, inert Resource for a type declared by the app's
// shared Resources.yaml that the 3D preview never renders - the Map itself
// and entity prototypes. ResourceManager needs a factory for every type it
// finds while scanning a location, but nothing here ever creates or loads
// these records, so no definition factory is needed either. This is what
// keeps the editor from having to link the app DLL's Map/ProtoEntity
// implementations purely to read the same manifest the game reads.
class InertResourceFactory
    : public wp::application::resourcesystem::ResourceFactory {
 public:
  explicit InertResourceFactory(string const& type)
      : wp::application::resourcesystem::ResourceFactory(type) {
  }

  wp::application::resourcesystem::Resource* createResource(
      string const& name,
      string const& namesp,
      string const& source,
      map<string, string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location) override {
    return new wp::application::resourcesystem::Resource(
        name, namesp, getType(), source, tags, location);
  }
};

// What the preview's WorldRenderer looks up by name once it is constructed:
// the two world materials and the ProcMaterial catalog SubMaterialResolver
// reads. Creating and loading each of these pulls in its own dependencies -
// shaders, programs, textures, the catalog's TextFile - so nothing else in
// the manifest has to be created or loaded at all.
constexpr array<pair<char const*, char const*>, 3> previewResources{{
    {"Material.Default", "World"},
    {"Material.Horizontal2d", "World"},
    {"ProcMaterials", ""},
}};

}  // namespace

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

  // Every type the manifest declares needs a factory before the location is
  // scanned, or instantiating that record throws. ProcMaterial's pair comes
  // from BooleanWorldRender, exactly as the game DLL registers them (see
  // DLL.cpp); Map and ProtoEntity are app-only types the preview ignores.
  mResourceMgr->addResourceFactory(new ProcMaterialResourceFactory());
  mResourceMgr->addResourceDefinitionFactory(
      new ProcMaterialResourceDefinitionFactory());
  mResourceMgr->addResourceFactory(new InertResourceFactory("Map"));
  mResourceMgr->addResourceFactory(new InertResourceFactory("ProtoEntity"));

  filesystem::path manifest{BW_EDITOR_PROC_MATERIAL_MANIFEST};
  mResourceMgr->addResourceLocation(
      "Directory", manifest.parent_path().string(), manifest.filename().string());

  // addResourceLocation only records the location - scanning is what reads
  // the manifest and instantiates its records.
  mResourceMgr->scanLocations();

  for (auto const& [name, namesp] : previewResources) {
    auto resource = mResourceMgr->getResource(name, namesp);
    mResourceMgr->createResource(resource);
    mResourceMgr->loadResource(resource);
  }
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
