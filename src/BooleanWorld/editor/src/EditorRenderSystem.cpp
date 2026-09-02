#include "EditorRenderSystem.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/common/Logger.h>

#include <core-lua/CoreLua.h>
#include <core-lua/LuaScriptResource.h>

#include "EmbossingCatalog.h"
#include "EmbossingCatalogResourceDefinitionFactory.h"
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
// the two world materials plus the ProcMaterial and global Embossing catalogs
// SubMaterialResolver reads. Creating and loading each pulls in its own
// dependencies, so nothing else in the manifest has to be loaded.
constexpr array<pair<char const*, char const*>, 4> previewResources{{
    {"Material.Default", "World"},
    {"Material.Horizontal2d", "World"},
    {"ProcMaterials", ""},
    {"Embossing", ""},
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
  mScriptRuntime = make_unique<bw::core::ScriptRuntime>(
      [this](string const& message) { mLogger->info("Lua: " + message); });

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
  mResourceMgr->addResourceFactory(new EmbossingCatalogResourceFactory());
  bw::core::registerLuaScriptResourceType(*mResourceMgr);
  mResourceMgr->addResourceDefinitionFactory(
      new ProcMaterialResourceDefinitionFactory());
  mResourceMgr->addResourceDefinitionFactory(
      new EmbossingCatalogResourceDefinitionFactory());
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

  // Preload every ImageResource so the wall normal-map and mask pickers can
  // show thumbnails without paying a per-selection decode/upload cost. Each
  // is acquired so World dependency churn cannot unload it; a bad image is
  // skipped rather than taking the whole render system down with it.
  for (auto const& resource : mResourceMgr->getResourcesByType("Image")) {
    try {
      mResourceMgr->createResource(resource);
      mResourceMgr->loadResource(resource);
      mResourceMgr->acquireResource(resource);
      mPreloadedImages.push_back(resource);
    } catch (std::exception const& exception) {
      mLogger->warn(std::string("Could not preload ImageResource ") +
                    resource->getQualifiedName() + ": " + exception.what());
    }
  }

  // Register only after the runtime and resource system have both completed
  // startup, so every deserialized RunScript receives this host's live
  // runtime (ADR-0038).
  bw::core::registerScriptStepTypes(*mScriptRuntime);
}

bool EditorRenderSystem::loadWorldDependencies(
    vector<string> const& resourceNames, string const& currentNamespace,
    string* error) {
  vector<wp::application::resourcesystem::ResourcePtr> candidate;
  try {
    for (auto const& reference : resourceNames) {
      string namesp;
      string name;
      wp::application::resourcesystem::Resource::splitName(
          reference, currentNamespace, &namesp, &name);
      auto resource = mResourceMgr->acquireResource(name, namesp);
      try {
        mResourceMgr->createResource(resource);
        mResourceMgr->loadResource(resource);
        if (auto script = dynamic_pointer_cast<bw::core::LuaScriptResource>(
                resource)) {
          // Keep the exact authored spelling as the runtime key. In
          // particular, an unqualified name resolved in the World namespace
          // must still be executable by that unqualified RunScript name.
          try {
            script->loadInto(*mScriptRuntime, reference);
          } catch (bw::core::ScriptException const& exception) {
            // Keep the dependency resolved: ScriptRuntime caches this compile
            // failure so the deserialized RunScript can report it as a failed
            // build step alongside ordinary execution failures (#367).
            mLogger->error(exception.what());
          }
        }
      } catch (...) {
        mResourceMgr->releaseResource(resource);
        throw;
      }
      candidate.push_back(move(resource));
    }
  } catch (exception const& exception) {
    for (auto const& resource : candidate) {
      mResourceMgr->releaseResource(resource);
    }
    if (error) *error = exception.what();
    return false;
  }

  for (auto const& resource : mWorldDependencies) {
    mResourceMgr->releaseResource(resource);
  }
  mWorldDependencies = move(candidate);
  return true;
}

void EditorRenderSystem::reloadProcMaterial(string const& resourceName) {
  auto resource = mResourceMgr->getResource(resourceName);
  // The preview loaded this resource directly rather than acquiring it, so a
  // release unloads it and its TextFile dependency. Loading it again then
  // rereads the YAML the authoring library just saved.
  mResourceMgr->releaseResource(resource);
  mResourceMgr->loadResource(resource);
}

void EditorRenderSystem::reloadEmbossingCatalog(string const& resourceName) {
  auto resource = mResourceMgr->getResource(resourceName);
  mResourceMgr->releaseResource(resource);
  mResourceMgr->loadResource(resource);
}

bool EditorRenderSystem::loadLuaScript(
    string const& resourceName, string* error) {
  try {
    string namesp;
    string name;
    wp::application::resourcesystem::Resource::splitName(
        resourceName, "World", &namesp, &name);
    auto resource = mResourceMgr->getResource(name, namesp);
    auto script = dynamic_pointer_cast<bw::core::LuaScriptResource>(resource);
    if (!script) {
      if (error) *error = "The selected resource is not a Lua script.";
      return false;
    }

    mResourceMgr->createResource(resource);
    mResourceMgr->loadResource(resource);
    try {
      script->loadInto(*mScriptRuntime, resourceName);
    } catch (bw::core::ScriptException const& exception) {
      // Compilation failures are authored script failures, not picker
      // failures. ScriptRuntime retained this one under resourceName.
      mLogger->error(exception.what());
    }
    return true;
  } catch (exception const& exception) {
    if (error) *error = exception.what();
    return false;
  }
}

bool EditorRenderSystem::reloadLuaScript(
    string const& resourceName, string* error) {
  wp::application::resourcesystem::ResourcePtr resource;
  bool retainedForWorld = false;
  bool released = false;
  try {
    string namesp;
    string name;
    wp::application::resourcesystem::Resource::splitName(
        resourceName, "World", &namesp, &name);
    resource = mResourceMgr->getResource(name, namesp);
    auto script = dynamic_pointer_cast<bw::core::LuaScriptResource>(resource);
    if (!script) {
      if (error) *error = "The selected resource is not a Lua script.";
      return false;
    }

    retainedForWorld = find(
        mWorldDependencies.begin(), mWorldDependencies.end(), resource) !=
        mWorldDependencies.end();

    // TextFileResource reads its source during create(), not load(). Release,
    // destroy, and recreate it so an external file edit is actually observed.
    mResourceMgr->releaseResource(resource);
    released = true;
    mResourceMgr->destroyResources({resource});
    mResourceMgr->createResource(resource);
    mResourceMgr->loadResource(resource);
    if (retainedForWorld) {
      mResourceMgr->acquireResource(resource);
      released = false;
    }

    mScriptRuntime->reload(resourceName, script->getText());
    return true;
  } catch (exception const& exception) {
    // Preserve the World's ownership count even when re-reading or compiling
    // fails. A compile failure is already retained atomically by the runtime
    // and has rebuilt its naming Layers before arriving here.
    if (resource && retainedForWorld && released) {
      mResourceMgr->acquireResource(resource);
    }
    if (error) *error = exception.what();
    return false;
  }
}

namespace {

// The one instance the process may ever hold - see the header for why a
// second one, even after this is destroyed, cannot be constructed.
unique_ptr<EditorRenderSystem> instance;

}  // namespace

void createEditorRenderSystem(int width, int height) {
  if (instance) {
    return;
  }
  instance = make_unique<EditorRenderSystem>(width, height);
}

EditorRenderSystem* editorRenderSystem() {
  return instance.get();
}

void destroyEditorRenderSystem() {
  instance.reset();
}

EditorRenderSystem::~EditorRenderSystem() {
  for (auto const& resource : mWorldDependencies) {
    mResourceMgr->releaseResource(resource);
  }
  mWorldDependencies.clear();

  for (auto const& resource : mPreloadedImages) {
    mResourceMgr->releaseResource(resource);
  }
  mPreloadedImages.clear();

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
