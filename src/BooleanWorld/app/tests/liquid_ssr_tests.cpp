#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string read(std::filesystem::path const& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

void gameplayUsesThePostWaterWorld() {
  auto app = std::filesystem::path(BW_APP_RESOURCE_DIR).parent_path();
  auto state = read(app / "src" / "StatePlayBooleanWorld.cpp");
  require(state.find("output.image = \"WaterComposite\"") != std::string::npos,
          "gameplay does not select WaterComposite");
  require(state.find("options.generatedWater = true") != std::string::npos,
          "gameplay does not opt into generated water topology");
  require(state.find("preWaterOutputImage + 2u") != std::string::npos,
          "gameplay does not address the post-water graph image");

  auto renderer = read(
      app.parent_path() / "render" / "src" / "WorldRenderer3d.cpp");
  require(renderer.find("mSurfaceSet == WorldSurfaceSet::Liquid && "
                        "mDeferToWaterPass") != std::string::npos,
          "the dedicated Liquid scene model is not deferred to WaterScene");
}

void liquidShaderPreservesTheSsrContract() {
  auto shader = read(
      std::filesystem::path(BW_APP_RESOURCE_DIR) / "shaders" /
      "world_pbr.frag");
  require(shader.find("PBR_SCENE_COLOUR_RESOLVED") != std::string::npos &&
              shader.find("PBR_SCENE_DEPTH") != std::string::npos,
          "Liquid does not sample the water-pass scene inputs");
  require(shader.find("gl_FragCoord.z > liquidSceneDepth") !=
              std::string::npos,
          "Liquid does not reject opaque-depth occlusion");
  require(shader.find("texelFetch(@Texture(PBR_SCENE_DEPTH)") !=
                  std::string::npos &&
              shader.find("liquidDither(gl_FragCoord.xy)") !=
                  std::string::npos &&
              shader.find("for (int refine = 0; refine < 5; ++refine)") !=
                  std::string::npos &&
              shader.find("liquidHitStability") != std::string::npos,
          "Liquid SSR lost point depth, dither, refinement, or hit stability");
  require(shader.find("else if (armed)") != std::string::npos &&
              shader.find("thickness * 0.5, thickness") !=
                  std::string::npos,
          "Liquid SSR lost sign-change marching or refined thickness rejection");
  require(shader.find("liquidRippleNormal") != std::string::npos &&
              shader.find("phaseA") != std::string::npos &&
              shader.find("phaseB") != std::string::npos,
          "Liquid reflection does not use fixed two-octave ripples");
  require(shader.find("mix(fallback, hitColour, confidence)") !=
                  std::string::npos &&
              shader.find("smoothstep(0.1, 0.35, nDotV)") !=
                  std::string::npos,
          "Liquid misses do not fade through confidence to ambient fallback");
  require(shader.find("@Uniform(LIQUID_REFLECTANCE)") != std::string::npos &&
              shader.find("@Uniform(LIQUID_F0)") != std::string::npos &&
              shader.find("vec4(reflectionColour, alpha)") !=
                  std::string::npos,
          "Liquid reflection is not Fresnel-alpha-composited over absorption");
}

}  // namespace

int main() {
  try {
    gameplayUsesThePostWaterWorld();
    liquidShaderPreservesTheSsrContract();
    std::cout << "Gameplay Liquid SSR contract passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
