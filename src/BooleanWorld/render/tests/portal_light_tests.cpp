#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <PortalLight.h>

namespace {
void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float left, float right, float tolerance = 1e-4f) {
  return std::abs(left - right) <= tolerance;
}

bw::core::ResolvedPortalPair portalPair(float resolvedWidth = 2.0f) {
  bw::core::ResolvedPortalPair pair;
  pair.layerId = 3;
  pair.pairId = 7;
  pair.active = true;
  pair.endpoints[0].endpointId = 0;
  pair.endpoints[0].resolved = true;
  pair.endpoints[0].aperture = {
      {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
      resolvedWidth, 0.0f, 4.0f, {0}};
  pair.endpoints[1].endpointId = 1;
  pair.endpoints[1].resolved = true;
  pair.endpoints[1].aperture = {
      {10.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f},
      resolvedWidth, 0.0f, 4.0f, {1}};
  return pair;
}

void canonicalTransformPreservesThePlayerTorch() {
  auto pair = portalPair();
  bw::app::PlayerTorchOptions options;
  options.attenuationRadius = 91.0f;
  options.attenuationFalloff = 17.0f;
  glm::vec3 realPosition{0.0f, 2.0f, -2.0f};
  auto attachment = BuildPortalLightAttachment(
      pair, 0, realPosition, options);
  require(attachment.has_value(),
          "a clear one-hop path produced no virtual Player Torch");

  auto canonical = bw::core::BuildPortalRigidTransform(pair, 0);
  auto expectedPlane = canonical.transformPoint({0.0f, 2.0f});
  require(near(attachment->position.x, expectedPlane.x) &&
              near(attachment->position.y,
                   canonical.transformElevation(realPosition.y)) &&
              near(attachment->position.z, -expectedPlane.y),
          "the virtual Player Torch did not use the canonical Portal transform");
  require(attachment->radiance == glm::vec3(PlayerTorchRadiance) &&
              attachment->attenuationRadius == options.attenuationRadius &&
              attachment->attenuationFalloff == options.attenuationFalloff,
          "virtual Player Torch colour, intensity, or falloff changed");

  auto widerAuthored = portalPair(2.0f);
  widerAuthored.endpoints[0].authored.width = 20.0f;
  auto normalized = BuildPortalLightAttachment(
      widerAuthored, 0, realPosition, options);
  require(normalized && normalized->radiance == attachment->radiance,
          "authored width normalization scaled transmitted radiance");

  realPosition.z = 2.0f;
  require(!BuildPortalLightAttachment(pair, 0, realPosition, options),
          "a Player Torch behind the source endpoint created a spill path");
}

void apertureGateRejectsFrameAndBackSideReceivers() {
  auto pair = portalPair();
  auto light = *BuildPortalLightAttachment(
      pair, 0, {0.0f, 2.0f, -2.0f}, bw::app::PlayerTorchOptions{});

  require(PortalLightAdmitsReceiver(light, {8.0f, 2.0f, 0.0f}),
          "a receiver through the resolved aperture was not admitted");
  require(!PortalLightAdmitsReceiver(light, {8.0f, 2.0f, -2.1f}),
          "a receiver immediately beside the aperture projection was lit");
  require(!PortalLightAdmitsReceiver(light, {12.5f, 2.0f, 0.0f}),
          "a receiver behind the Portal frame was lit");
  require(!PortalLightAdmitsReceiver(light, {8.0f, 6.1f, 0.0f}),
          "a receiver path above the resolved aperture was lit");
}

void attachmentsAreBoundedAndPassScoped() {
  auto light = *BuildPortalLightAttachment(
      portalPair(), 0, {0.0f, 2.0f, -2.0f},
      bw::app::PlayerTorchOptions{});
  mpp::AuxiliarySceneView pass;
  require(AttachPortalLightsToPass(pass, std::span(&light, 1)),
          "one virtual Player Torch did not attach to its pass");
  auto const& uniforms = pass.uniformOverrides.getUniformData();
  require(uniforms.contains("PORTAL_LIGHT_COUNT") &&
              uniforms.contains("PORTAL_LIGHT_POSITION") &&
              uniforms.contains("PORTAL_LIGHT_APERTURE_BOUNDS"),
          "the auxiliary pass did not own complete virtual-light state");

  mpp::AuxiliarySceneView laterPass;
  require(laterPass.uniformOverrides.getNumUniforms() == 0,
          "virtual-light state contaminated a later auxiliary pass");

  std::vector<PortalLightAttachment> overflow(
      PortalLightAttachmentLimit + 1, light);
  mpp::AuxiliarySceneView boundedPass;
  require(!AttachPortalLightsToPass(boundedPass, overflow) &&
              boundedPass.uniformOverrides.getNumUniforms() == 0,
          "an over-budget virtual-light list did not fail to no spill");
}
}  // namespace

int main() {
  try {
    canonicalTransformPreservesThePlayerTorch();
    apertureGateRejectsFrameAndBackSideReceivers();
    attachmentsAreBoundedAndPassScoped();
    std::cout << "One-hop Portal Player Torch lighting passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
