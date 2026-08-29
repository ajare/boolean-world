#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string read(std::filesystem::path const& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not read " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), {}};
}

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void shaderHasNoMaterialOrLightingWork() {
  auto resources = std::filesystem::path(BW_APP_RESOURCE_DIR);
  auto shader = read(resources / "shaders" / "fragment_overdraw.frag");
  require(shader.find("@Out(vec4 COLOUR)") != std::string::npos,
          "fragment overdraw shader does not write scene colour");
  require(shader.find("LIGHT") == std::string::npos &&
              shader.find("@Texture") == std::string::npos &&
              shader.find("MATERIAL") == std::string::npos,
          "fragment overdraw shader performs material or lighting work");

  auto catalog = read(resources / "Resources.yaml");
  require(catalog.find("FragmentOverdrawShader") != std::string::npos &&
              catalog.find("FragmentOverdrawProgram") != std::string::npos &&
              catalog.find("FragmentOverdrawResolveProgram") !=
                  std::string::npos &&
              catalog.find("Material.FragmentOverdraw") != std::string::npos,
          "fragment overdraw shader is not a loadable world material");
}

void appOffersDepthPrepassToggle() {
  auto appRoot = std::filesystem::path(BW_APP_RESOURCE_DIR).parent_path();
  auto header = read(appRoot / "include" / "StatePlayBooleanWorld.h");
  auto state = read(appRoot / "src" / "StatePlayBooleanWorld.cpp");
  require(header.find("bool depthPrepass{true}") != std::string::npos,
          "depth pre-pass does not retain the enabled default");
  require(header.find("bool sortGeometryFrontToBack{false}") !=
              std::string::npos,
          "closest-first geometry sorting is not disabled by default");
  require(state.find("ImGui::Checkbox(\"Depth pre-pass\"") !=
                  std::string::npos &&
              state.find("options.depthPrepass = mDebugDisplay.depthPrepass") !=
                  std::string::npos,
          "F5 depth pre-pass option is not connected to pipeline creation");
  require(state.find(".depth-prepass") != std::string::npos &&
              state.find(".no-depth-prepass") != std::string::npos &&
              state.find("mWorldRenderPipelines[key]") !=
                  std::string::npos,
          "enabled and disabled depth pre-pass modes do not use distinct pipelines");
}

void appOffersLiveWedgeQuality() {
  auto appRoot = std::filesystem::path(BW_APP_RESOURCE_DIR).parent_path();
  auto header = read(appRoot / "include" / "StatePlayBooleanWorld.h");
  auto state = read(appRoot / "src" / "StatePlayBooleanWorld.cpp");
  require(header.find("int wedgeQuality{0}") != std::string::npos &&
              state.find("\"Wedge quality\", &mDebugDisplay.wedgeQuality, 0, 3") !=
                  std::string::npos &&
              state.find("wedgeSettings.quality =") != std::string::npos &&
              state.find("generator->generate()") != std::string::npos,
          "F5 Wedge quality does not regenerate session geometry in range 0-3");
}

void appUsesAPostProcessFreeDebugPath() {
  auto appRoot = std::filesystem::path(BW_APP_RESOURCE_DIR).parent_path();
  auto state = read(appRoot / "src" / "StatePlayBooleanWorld.cpp");
  require(state.find("Visualize fragment overdraw") != std::string::npos &&
              state.find("setFragmentOverdraw(mDebugDisplay.fragmentOverdraw)") !=
                  std::string::npos,
          "F5 does not toggle the fragment overdraw world material");

  auto pipelineBegin = state.find("getOrCreateFragmentOverdrawPipeline(");
  auto pipelineEnd = state.find("void StatePlayBooleanWorld::setupMapRenderer", pipelineBegin);
  require(pipelineBegin != std::string::npos && pipelineEnd != std::string::npos,
          "fragment overdraw pipeline is missing");
  auto pipeline = state.substr(pipelineBegin, pipelineEnd - pipelineBegin);
  require(pipeline.find("SceneLdr") != std::string::npos &&
              pipeline.find(
                  "options.depthPrepass = mDebugDisplay.depthPrepass") !=
                  std::string::npos &&
              pipeline.find("ambientOcclusion") == std::string::npos &&
              pipeline.find("joinPlayerTorchShadowDomain") == std::string::npos &&
              pipeline.find("antiAliasing") == std::string::npos,
          "fragment overdraw pipeline still enables a post-process or shadow pass");

  require(state.find("if (mDebugDisplay.fragmentOverdraw) {") != std::string::npos &&
              state.find("mFragmentOverdrawResolveProgram") !=
                  std::string::npos,
          "fragment overdraw output does not remove the one-fragment baseline");

  auto resolveShader =
      read(std::filesystem::path(BW_APP_RESOURCE_DIR) / "shaders" /
           "fragment_overdraw_resolve.frag");
  require(resolveShader.find("accumulated.r - alpha") != std::string::npos,
          "fragment overdraw resolve does not report zero for one fragment");

  auto renderer = read(appRoot.parent_path() / "render" / "src" / "WorldRenderer3d.cpp");
  require(renderer.find("setMeshMaterial(meshName, material)") != std::string::npos &&
              renderer.find("enabled || mBlendedMeshNames.count(meshName)") !=
                  std::string::npos &&
              renderer.find("setMeshDepthPrepass(") != std::string::npos,
          "world meshes do not accumulate overdraw or participate in its depth prepass");

  // Turning the diagnostic off restores each mesh's own blend classification.
  // Forcing every mesh opaque discards the alpha a liquid surface writes, and
  // the liquid then hides the absorbed geometry it exists to be seen through.
  require(renderer.find("mBlendedMeshNames.insert(meshName)") != std::string::npos,
          "liquid surfaces are not recorded as blended in their own right");
}

}  // namespace

int main() {
  try {
    shaderHasNoMaterialOrLightingWork();
    appOffersDepthPrepassToggle();
    appOffersLiveWedgeQuality();
    appUsesAPostProcessFreeDebugPath();
    std::cout << "Fragment overdraw debug rendering contract passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
