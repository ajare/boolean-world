#include "willpower/viz/DynamicRenderer.h"

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

DynamicRenderer::DynamicRenderer(string const& name, string const& type, mpp::ResourceManager* renderResourceMgr)
    : Renderer(name, type, renderResourceMgr) {
}

}  // namespace viz
}  // namespace WP_NAMESPACE
