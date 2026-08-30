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

void everyWorldPathUsesThePostWaterWorld() {
  auto app = std::filesystem::path(BW_APP_RESOURCE_DIR).parent_path();
  auto state = read(app / "src" / "StatePlayBooleanWorld.cpp");
  require(state.find(": \"WaterComposite\"") != std::string::npos,
          "gameplay does not select WaterComposite");
  require(state.find("options.generatedWater = true") != std::string::npos &&
              state.find("WaterReflectionTechnique::ScreenSpace") !=
                  std::string::npos &&
              state.find("WaterReflectionTechnique::Planar") !=
                  std::string::npos &&
              state.find("discoverLiquidReflectionPlanes") !=
                  std::string::npos,
          "gameplay does not select generated Water technique and Planar planes");
  require(state.find("planarPlanes.size()) + 1u") != std::string::npos &&
              state.find("2u * static_cast<std::uint32_t>(planarPlanes.size())") !=
                  std::string::npos,
          "gameplay does not address zero-through-four Planar graph outputs");
  require(state.find("if (mDebugDisplay.fragmentOverdraw) {") !=
                  std::string::npos &&
              state.find("outputImage = preWaterOutputImage") !=
                  std::string::npos,
          "fragment-overdraw diagnostics no longer retain their pre-water output");
  require(state.find("Override liquid reflectance") != std::string::npos &&
              state.find("Override liquid F0") != std::string::npos &&
              state.find("Enable water reflections") != std::string::npos &&
              state.find("Water reflection technique") != std::string::npos &&
              state.find("Planar reflection resolution") !=
                  std::string::npos &&
              state.find("Reflection mip level##Liquid") != std::string::npos,
          "gameplay F5 options do not expose generic Water reflection controls");
  require(state.find("RenderGraphCapture\", {Key::F10}") !=
                  std::string::npos &&
              state.find("requestGraphImageCapture") != std::string::npos &&
              state.find("capture.passName") != std::string::npos &&
              state.find("stats.primaryColourOutputName") != std::string::npos &&
              state.find("stats.primaryColourOutputWidth") != std::string::npos,
          "gameplay F10 and telemetry do not expose named graph images and dimensions");
  require(state.find("planarReflectionRuntimeFailed") != std::string::npos &&
              state.find("mPlanarReflectionSessionFailed = true") !=
                  std::string::npos &&
              state.find("technique remains Planar") != std::string::npos,
          "gameplay does not retain Planar selection after a runtime reflection failure");

  auto preview = read(app.parent_path() / "editor" / "src" /
                      "PreviewRenderScene.cpp");
  require(preview.find("output.image = \"WaterComposite\"") !=
                  std::string::npos &&
              preview.find("options.generatedWater = true") !=
                  std::string::npos &&
              preview.find("WaterReflectionTechnique::ScreenSpace") !=
                  std::string::npos &&
              preview.find("constexpr std::uint32_t outputImageIndex = 6u") !=
                  std::string::npos,
          "editor preview does not select the generated post-water output");
  require(preview.find("WorldRenderer::WallRenderVariantResolver{}, \"World\", true") !=
              std::string::npos,
          "editor preview does not defer Liquid into WaterScene");

  auto renderer = read(
      app.parent_path() / "render" / "src" / "WorldRenderer3d.cpp");
  require(renderer.find("mSurfaceSet == WorldSurfaceSet::Liquid && "
                        "mDeferToWaterPass") != std::string::npos,
          "the dedicated Liquid scene model is not deferred to WaterScene");
  require(renderer.find("mDeferToWaterPass && liquidReflectionEnabled") !=
                  std::string::npos &&
              renderer.find("LIQUID_WATER_PASS_ENABLED") != std::string::npos &&
              renderer.find("LIQUID_REFLECTION_ENABLED") !=
                  std::string::npos,
          "the generic Liquid reflection toggle is not independent from compositing");
  require(renderer.find(
              "if (mSurfaceSet == WorldSurfaceSet::Liquid) {\n      continue;") !=
                  std::string::npos,
          "Liquid buckets can be poisoned by authored floor/ceiling uniforms");
  require(renderer.find(
              "updateUniform(\"LIQUID_REFLECTANCE\", liquid.reflectance)") !=
                  std::string::npos &&
              renderer.find(
                  "liquidReflectanceOverride.value_or(properties.reflectance)") !=
                  std::string::npos,
          "Liquid reflectance does not replace its inert default or accept a runtime override");
}

void liquidShaderPreservesTheSsrContract() {
  auto shader = read(
      std::filesystem::path(BW_APP_RESOURCE_DIR) / "shaders" /
      "world_pbr.frag");
  require(shader.find("PBR_SCENE_COLOUR_RESOLVED") != std::string::npos &&
              shader.find("PBR_SCENE_DEPTH") != std::string::npos &&
              shader.find("PBR_PLANAR_REFLECTION_0") != std::string::npos &&
              shader.find("PBR_PLANAR_REFLECTION_3") != std::string::npos &&
              shader.find("LIQUID_WATER_PASS_ENABLED") != std::string::npos &&
              shader.find("float alpha = reflectionEnabled") !=
                  std::string::npos,
          "Liquid does not independently gate compositing and reflection technique");
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
                  std::string::npos &&
              shader.find("planarSample.a * edgeFade") !=
                  std::string::npos &&
              shader.find("MPP_PLANAR_REFLECTION_VIEW_PROJECTION_0") !=
                  std::string::npos &&
              shader.find("MPP_PLANAR_REFLECTION_VIEW_PROJECTION_3") !=
                  std::string::npos &&
              shader.find("MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_3") !=
                  std::string::npos &&
              shader.find("planarIndex == 3") != std::string::npos,
          "Liquid reflection misses, image edges, or grouped Planar elevations do not select the matching fallback-aware image");
  require(shader.find("@Uniform(LIQUID_REFLECTANCE)") != std::string::npos &&
              shader.find("@Uniform(LIQUID_F0)") != std::string::npos &&
              shader.find("@Uniform(LIQUID_REFLECTION_MIP_LEVEL)") !=
                  std::string::npos &&
              shader.find("vec4(reflectionColour, alpha)") !=
                  std::string::npos,
          "Liquid reflection is not runtime-tunable or Fresnel-composited over absorption");

  auto horizontalShader = read(
      std::filesystem::path(BW_APP_RESOURCE_DIR) / "shaders" /
      "world_pbr_2d.frag");
  for (auto const* source : {&shader, &horizontalShader}) {
    require(source->find("@@Uniform(int MPP_VIRTUAL_CAMERA)") !=
                    std::string::npos &&
                source->find("if (@Uniform(MPP_VIRTUAL_CAMERA) != 0)") !=
                    std::string::npos &&
                source->find("outTransmittance = vec3(1.0)") !=
                    std::string::npos &&
                source->find("return direct + ambient") != std::string::npos,
            "a reflected virtual camera can still invent ordinary Liquid absorption");
  }
}

}  // namespace

int main() {
  try {
    everyWorldPathUsesThePostWaterWorld();
    liquidShaderPreservesTheSsrContract();
    std::cout << "Gameplay Liquid SSR contract passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
