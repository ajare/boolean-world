#pragma once

#include <string>
#include <vector>
#include <memory>

namespace mpp {
class Logger;
class RenderSystem;
class ResourceManager;
}  // namespace mpp

namespace bw {
namespace core {
class ScriptRuntime;
}  // namespace core
}  // namespace bw

namespace wp {
class Logger;
namespace application {
namespace resourcesystem {
class ResourceManager;
class Resource;
}  // namespace resourcesystem
}  // namespace application
}  // namespace wp

namespace editor {

// Bootstraps mpp::RenderSystem/mpp::ResourceManager and the manifest-driven
// wp::application::resourcesystem::ResourceManager against an already
// current SDL3 GL context (the editor's own window - see the extern
// SDL_Window* gWindow in Preview3D.cpp). Constructs no window/context of its
// own. See GitHub issue #268 and ADR-0025 for why the editor needs a second,
// independent resource-manager view of the same on-disk ProcMaterial data
// that ProcMaterialLibrary already loads directly.
//
// Construction also scans the manifest and loads exactly the resources the
// preview's WorldRenderer looks up by name (the two world Materials and the
// ProcMaterial catalog), along with their dependencies. Nothing else in the
// manifest is created or loaded.
//
// Constructed once while the editor starts up and kept for the rest of the
// process - createCoreResources()'s shader/pipeline compilation and the
// manifest scan are one-time costs worth paying before the first frame
// rather than stalling the first 3D preview open.
//
// At most one instance may exist per process, ever - even sequentially.
// wp::application::resourcesystem::ResourceManager registers its default
// ResourceDefinitionFactories (TextFile, Image, ...) into a process-wide
// static registry (Resource::msResourceDefinitionFactories) that its
// destructor never erases, only deletes the pointers in; a second instance's
// constructor then throws "already registered" against now-dangling
// entries. Launcher's own gResourceMgr already relies on this same
// single-instance-for-the-process assumption, so this is an existing
// library constraint, not something introduced here.
class EditorRenderSystem {
public:
  // width/height seed RenderSystem's initial viewport. They do not track
  // window resizes; nothing in this ticket's scope renders through them yet.
  EditorRenderSystem(int width, int height);
  ~EditorRenderSystem();

  EditorRenderSystem(EditorRenderSystem const&) = delete;
  EditorRenderSystem& operator=(EditorRenderSystem const&) = delete;

  mpp::RenderSystem* renderSystem() const { return mRenderSystem; }
  mpp::ResourceManager* renderResourceManager() const {
    return mRenderResourceMgr;
  }
  wp::application::resourcesystem::ResourceManager* resourceManager() const {
    return mResourceMgr;
  }
  wp::Logger* logger() const { return mLogger; }

  // Re-reads a ProcMaterial and its YAML dependency after editor authoring
  // saves it directly to disk.
  void reloadProcMaterial(std::string const& resourceName);
  // Re-reads the sole global Embossing catalog after an editor save.
  void reloadEmbossingCatalog(std::string const& resourceName);

  // Resolves one browser-selected Lua script and compiles it under the exact
  // World-relative spelling the RunScript step will store. A syntax error is
  // still a successful load: ScriptRuntime caches it so the step can report
  // the ordinary build failure in its panel.
  bool loadLuaScript(std::string const& resourceName,
                     std::string* error = nullptr);

  // Re-reads an externally edited Lua script, replaces its runtime cache
  // entry, and rebuilds exactly the Layers whose RunScript steps name it.
  bool reloadLuaScript(std::string const& resourceName,
                       std::string* error = nullptr);

  // Atomically replaces the resources retained for the active World.
  bool loadWorldDependencies(std::vector<std::string> const& resourceNames,
                             std::string const& currentNamespace,
                             std::string* error = nullptr);

private:
  mpp::Logger* mMppLogger{};
  wp::Logger* mLogger{};
  mpp::RenderSystem* mRenderSystem{};
  mpp::ResourceManager* mRenderResourceMgr{};
  wp::application::resourcesystem::ResourceManager* mResourceMgr{};
  std::unique_ptr<bw::core::ScriptRuntime> mScriptRuntime;
  std::vector<std::shared_ptr<
      wp::application::resourcesystem::Resource>> mWorldDependencies;
  // Every ImageResource the manifest declares, created, loaded, and acquired
  // at construction so wall normal-map/mask pickers can show thumbnails
  // without per-selection loads. Released while the GL context is current.
  std::vector<std::shared_ptr<
      wp::application::resourcesystem::Resource>> mPreloadedImages;
};

// Constructs the process-wide instance against the already-current GL
// context. Called once from the editor's startup, after the SDL/GL context
// and GLEW are up. Throws what the constructor throws; the editor treats a
// failure as non-fatal and runs on without a 3D preview.
void createEditorRenderSystem(int width, int height);

// Null until createEditorRenderSystem() has succeeded.
[[nodiscard]] EditorRenderSystem* editorRenderSystem();

// Must run while the editor's GL context is still current, so the editor's
// shutdown calls this before destroying it.
void destroyEditorRenderSystem();

}  // namespace editor
