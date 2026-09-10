#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

std::string readFile(char const* path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error(std::string("Could not read ") + path);
  return {(std::istreambuf_iterator<char>(input)), {}};
}

void resourceShapeExposesOnlyAcceptedTriplanarControls() {
  auto schema = readFile(BW_TRIPLANAR_SCHEMA);
  require(schema.find("\"Albedo\"") != std::string::npos &&
              schema.find("\"TileWidth\"") != std::string::npos &&
              schema.find("\"BlendSharpness\"") != std::string::npos,
          "Triplanar schema lost an accepted authoring control");
  for (auto const* rejected : {"NormalMap", "normalMap", "Chip", "chip",
                               "Acoustic", "acoustic", "Opacity", "opacity",
                               "Metallic", "Roughness"}) {
    require(schema.find(rejected) == std::string::npos,
            std::string("Triplanar schema exposes rejected control: ") +
                rejected);
  }
}

void shadersKeepTriplanarOpaqueAndComposeRelief() {
  for (auto const* path : {BW_WORLD_PBR_SHADER, BW_WORLD_PBR_2D_SHADER}) {
    auto shader = readFile(path);
    auto sample = shader.find("vec3 triplanarAlbedo");
    auto rgb = shader.find(".rgb;", sample);
    auto branch = shader.find("if (usesTriplanar)");
    auto emboss = shader.find("material.normal = embossSurface", branch);
    require(sample != std::string::npos && rgb != std::string::npos &&
                branch != std::string::npos && emboss != std::string::npos &&
                branch < emboss,
            "Triplanar shader lost RGB-only albedo or later Emboss composition");
    require(shader.find("material.metallic = 0.0", branch) < emboss &&
                shader.find("material.roughness = 0.7", branch) < emboss,
            "Triplanar shader gained configurable PBR semantics");
  }
}

void rendererKeepsMasksDormantButNormalMapsActive() {
  auto renderer = readFile(BW_WORLD_RENDERER3D_SOURCE);
  require(renderer.find(
              "if (!resolved.isTriplanar() && variant.setMaskUniforms)") !=
              std::string::npos,
          "Triplanar wall variants no longer suppress wall-mask uniforms");
  auto normalMap = renderer.find("if (variant.setUniforms)");
  auto dormantMask = renderer.find(
      "if (!resolved.isTriplanar() && variant.setMaskUniforms)");
  require(normalMap != std::string::npos && normalMap < dormantMask,
          "wall normal-map override no longer composes independently of the dormant mask");
  require(renderer.find("params->setMeshBlend(meshName, false)") !=
              std::string::npos,
          "ordinary surface buckets no longer remain opaque");
}
}  // namespace

int main() {
  try {
    resourceShapeExposesOnlyAcceptedTriplanarControls();
    shadersKeepTriplanarOpaqueAndComposeRelief();
    rendererKeepsMasksDormantButNormalMapsActive();
    std::cout << "Triplanar surface-composition contract passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
