#pragma once

#include <string>

namespace mpp {
class Logger;
class RenderSystem;
class ResourceManager;
}  // namespace mpp

namespace wp {
class Logger;
namespace application {
namespace resourcesystem {
class ResourceManager;
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
// Meant to be lazily constructed on first 3D preview open and kept for the
// rest of the editor process - createCoreResources()'s shader/pipeline
// compilation and the manifest scan are one-time costs worth amortising,
// not rebuilding per preview open/close.
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

private:
  mpp::Logger* mMppLogger{};
  wp::Logger* mLogger{};
  mpp::RenderSystem* mRenderSystem{};
  mpp::ResourceManager* mRenderResourceMgr{};
  wp::application::resourcesystem::ResourceManager* mResourceMgr{};
};

}  // namespace editor
