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

bw::core::ResolvedPortalLoop translationLoop(
    uint32_t loopId, float sourceY, float destinationY,
    float sourceX = 0.0f, float destinationX = 0.0f,
    float resolvedWidth = 2.0f) {
  bw::core::ResolvedPortalLoop portalLoop;
  portalLoop.endpoints.resize(2);
  portalLoop.traversalOrder = {0, 1};
  portalLoop.layerId = 3;
  portalLoop.loopId = loopId;
  portalLoop.active = true;
  portalLoop.endpoints[0].endpointId = 0;
  portalLoop.endpoints[0].resolved = true;
  portalLoop.endpoints[0].aperture = {
      {sourceX, sourceY}, {1.0f, 0.0f}, {0.0f, 1.0f}, resolvedWidth, 0.0f, 4.0f, {0}};
  portalLoop.endpoints[1].endpointId = 1;
  portalLoop.endpoints[1].resolved = true;
  portalLoop.endpoints[1].aperture = {
      {destinationX, destinationY}, {-1.0f, 0.0f}, {0.0f, -1.0f}, resolvedWidth, 0.0f, 4.0f, {1}};
  return portalLoop;
}

void canonicalTransformPreservesThePlayerTorch() {
  auto portalLoop = translationLoop(7, 0.0f, 10.0f);
  bw::app::PlayerTorchOptions options;
  options.attenuationRadius = 91.0f;
  options.attenuationFalloff = 17.0f;
  glm::vec3 realPosition{0.0f, 2.0f, -2.0f};
  auto attachment = BuildPortalLightAttachment(
      portalLoop, 0, realPosition, options);
  require(attachment.has_value(),
          "a clear one-hop path produced no virtual Player Torch");

  auto canonical = bw::core::BuildPortalMapping(portalLoop, 0);
  auto expectedPlane = canonical.transformPoint({0.0f, 2.0f});
  require(near(attachment->position.x, expectedPlane.x) &&
              near(attachment->position.y,
                   canonical.transformElevation(realPosition.y)) &&
              near(attachment->position.z, -expectedPlane.y),
          "the virtual Player Torch did not use the canonical Portal transform");
  require(attachment->radiance == glm::vec3(PlayerTorchRadiance) &&
              attachment->attenuationRadius == options.attenuationRadius &&
              attachment->attenuationFalloff == options.attenuationFalloff &&
              attachment->hops.size() == 1,
          "virtual Player Torch colour, falloff, or path changed");

  auto widerAuthored = portalLoop;
  widerAuthored.endpoints[0].authored.width = 20.0f;
  auto normalized = BuildPortalLightAttachment(
      widerAuthored, 0, realPosition, options);
  require(normalized && normalized->radiance == attachment->radiance,
          "authored width normalization scaled transmitted radiance");

  realPosition.z = 2.0f;
  require(!BuildPortalLightAttachment(portalLoop, 0, realPosition, options),
          "a Player Torch behind the source endpoint created a spill path");
}

void stableIdentityPreservesLightOutput() {
  std::vector loops{
      translationLoop(10, 0.0f, 10.0f),
      translationLoop(11, 8.0f, 18.0f)};
  glm::vec3 torch{0.0f, 2.0f, -2.0f};
  bw::app::PlayerTorchOptions options;
  auto baseline = PlanPortalLights(loops, torch, options);
  for (auto& portalLoop : loops) {
    portalLoop.endpoints[0].endpointId = 17;
    portalLoop.endpoints[1].endpointId = 93;
    portalLoop.traversalOrder = {93, 17};
    std::swap(portalLoop.endpoints[0], portalLoop.endpoints[1]);
  }
  auto actual = PlanPortalLights(loops, torch, options);
  require(!baseline.lights.empty() && actual.lights.size() == baseline.lights.size() &&
              actual.shadowPassCount == baseline.shadowPassCount &&
              actual.diagnostics.size() == baseline.diagnostics.size(),
          "endpoint identity changed light or shadow budgets");
  for (size_t index = 0; index < actual.lights.size(); ++index) {
    auto const& light = actual.lights[index];
    auto const& expected = baseline.lights[index];
    require(light.position == expected.position &&
                light.radiance == expected.radiance &&
                light.strength == expected.strength &&
                light.visibility == expected.visibility &&
                light.hops.size() == expected.hops.size(),
            "endpoint storage order changed virtual Torch output");
    for (size_t hop = 0; hop < light.hops.size(); ++hop) {
      require(light.path[hop].endpointId == 17 &&
                  light.hops[hop].sourceEndpoint.endpointId == 17 &&
                  light.hops[hop].destinationEndpoint.endpointId == 93 &&
                  light.hops[hop].destinationToSource ==
                      expected.hops[hop].destinationToSource,
              "light paths or shadow transforms used endpoint slots");
    }
    for (glm::vec3 receiver : {glm::vec3{0, 2, -16}, glm::vec3{4, 2, -16}}) {
      require(PortalLightAdmitsReceiver(light, receiver) ==
                  PortalLightAdmitsReceiver(expected, receiver),
              "stable endpoint routing changed aperture gating");
    }
  }
  auto attachment = BuildPortalLightAttachment(loops[0], 17, torch, options);
  require(attachment && attachment->path.front().endpointId == 17,
          "single-hop light API did not accept stable endpoint identity");
  require(!BuildPortalLightAttachment(loops[0], 0, torch, options),
          "single-hop light API interpreted a missing ID as a slot");
}

void multiHopCompositionGatesEveryAperture() {
  std::vector loops{
      translationLoop(10, 0.0f, 10.0f),
      translationLoop(11, 8.0f, 18.0f)};
  PortalLightLimits limits;
  limits.maxVirtualLights = PortalLightAttachmentLimit;
  auto plan = PlanPortalLights(
      loops, {0.0f, 2.0f, -2.0f}, bw::app::PlayerTorchOptions{}, limits);
  auto found = std::ranges::find_if(plan.lights, [](auto const& light) {
    return light.path.size() == 2 && light.path[0].loopId == 10 &&
           light.path[1].loopId == 11;
  });
  require(found != plan.lights.end(),
          "a visible two-hop Portal path was not retained");
  require(near(found->position.x, 0.0f) &&
              near(found->position.y, 2.0f) &&
              near(found->position.z, -22.0f),
          "multi-hop virtual-light transforms were not composed canonically");
  require(PortalLightAdmitsReceiver(*found, {0.0f, 2.0f, -16.0f}),
          "a receiver through both resolved apertures was not admitted");
  require(!PortalLightAdmitsReceiver(*found, {4.0f, 2.0f, -16.0f}),
          "a receiver whose folded ray misses an aperture was admitted");
  require(found->hops.size() + 1 == 3,
          "a two-hop path did not request all three folded shadow legs");
}

void endpointCyclesAndEquivalentLightsAreRejected() {
  auto first = translationLoop(20, 0.0f, 10.0f);
  auto equivalent = translationLoop(21, 0.0f, 10.0f);
  std::vector loops{first, equivalent};
  PortalLightLimits limits;
  limits.maxVirtualLights = PortalLightAttachmentLimit;
  auto plan = PlanPortalLights(
      loops, {0.0f, 2.0f, -2.0f}, bw::app::PlayerTorchOptions{}, limits);
  require(plan.count(PortalLightDiagnosticReason::EquivalentLight) != 0,
          "equivalent transformed virtual lights were not deduplicated");
  require(plan.count(PortalLightDiagnosticReason::EndpointCycle) != 0,
          "a path was allowed to revisit a traversed endpoint");
  for (auto const& light : plan.lights) {
    for (size_t left = 0; left < light.hops.size(); ++left) {
      for (size_t right = left + 1; right < light.hops.size(); ++right) {
        require(
            light.hops[left].sourceEndpoint !=
                    light.hops[right].sourceEndpoint &&
                light.hops[left].sourceEndpoint !=
                    light.hops[right].destinationEndpoint &&
                light.hops[left].destinationEndpoint !=
                    light.hops[right].sourceEndpoint &&
                light.hops[left].destinationEndpoint !=
                    light.hops[right].destinationEndpoint,
            "a retained path revisited an endpoint");
      }
    }
  }
}

void mutuallyVisibleLoopIsStableAndBounded() {
  std::vector loops{
      translationLoop(25, 0.0f, 10.0f),
      translationLoop(26, 8.0f, -2.0f)};
  auto first = PlanPortalLights(
      loops, {0.0f, 2.0f, -2.0f}, bw::app::PlayerTorchOptions{});
  auto second = PlanPortalLights(
      loops, {0.0f, 2.0f, -2.0f}, bw::app::PlayerTorchOptions{});
  require(first.count(PortalLightDiagnosticReason::EndpointCycle) != 0 &&
              first.shadowPassCount <= PortalLightShadowPassLimit &&
              first.lights.size() <= PortalLightAttachmentLimit,
          "a mutually visible Portal loop was not cycle- and budget-bounded");
  require(first.lights.size() == second.lights.size() &&
              first.shadowPassCount == second.shadowPassCount,
          "a Portal loop changed its retained light or pass count");
  for (size_t index = 0; index < first.lights.size(); ++index) {
    require(first.lights[index].path == second.lights[index].path,
            "a Portal loop churned its retained path ordering");
  }
}

void independentBudgetsUseDeterministicRanking() {
  std::vector loops{
      translationLoop(30, 0.0f, 10.0f),
      translationLoop(31, -4.0f, 20.0f),
      translationLoop(32, -8.0f, 30.0f)};
  auto torch = glm::vec3{0.0f, 2.0f, -2.0f};
  auto options = bw::app::PlayerTorchOptions{};

  PortalLightLimits hopLimited;
  hopLimited.maxHops = 1;
  auto hops = PlanPortalLights(loops, torch, options, hopLimited);
  require(hops.count(PortalLightDiagnosticReason::HopBudget) != 0,
          "the named hop budget did not diagnose deeper paths");

  PortalLightLimits lightLimited;
  lightLimited.maxHops = 1;
  lightLimited.maxVirtualLights = 1;
  auto first = PlanPortalLights(loops, torch, options, lightLimited);
  auto second = PlanPortalLights(loops, torch, options, lightLimited);
  require(first.lights.size() == 1 &&
              first.lights.front().path.front().loopId == 30 &&
              first.count(
                  PortalLightDiagnosticReason::VirtualLightBudget) != 0,
          "virtual-light budget did not retain the strongest path");
  require(first.lights.front().path == second.lights.front().path &&
              first.shadowPassCount == second.shadowPassCount,
          "stable input produced frame-to-frame light-list churn");

  PortalLightLimits shadowLimited;
  shadowLimited.maxHops = 1;
  shadowLimited.maxVirtualLights = PortalLightAttachmentLimit;
  shadowLimited.maxShadowPasses = 2;
  auto shadows = PlanPortalLights(loops, torch, options, shadowLimited);
  require(shadows.lights.size() == 1 && shadows.shadowPassCount == 2 &&
              shadows.count(
                  PortalLightDiagnosticReason::ShadowPassBudget) != 0,
          "shadow-pass budget was not independent of the light budget");

  PortalLightLimits disabled;
  disabled.maxHops = 0;
  auto none = PlanPortalLights(loops, torch, options, disabled);
  require(none.lights.empty() &&
              none.count(PortalLightDiagnosticReason::HopBudget) != 0,
          "recursive Portal lighting could not be disabled explicitly");
}

void attachmentsFailAtomicallyWithoutCompleteShadows() {
  auto light = *BuildPortalLightAttachment(
      translationLoop(40, 0.0f, 10.0f), 0,
      {0.0f, 2.0f, -2.0f}, bw::app::PlayerTorchOptions{});
  require(light.sourcePosition == glm::vec3(0.0f, 2.0f, -2.0f) &&
              light.hops.front().sourceApertureFront != glm::vec3{} &&
              light.hops.front().destinationToSource != glm::mat4{1.0f},
          "the one-hop attachment omitted its folded source-leg transform");

  PortalLightShadowAttachment incomplete;
  incomplete.light = light;
  incomplete.shadowRange = 192.0f;
  incomplete.mapTexelSize = 1.0f / 1024.0f;
  mpp::AuxiliarySceneView pass;
  require(!AttachPortalLightsToPass(pass, std::span(&incomplete, 1)) &&
              pass.uniformOverrides.getNumUniforms() == 0 &&
              pass.samplerOverrides.empty(),
          "an incomplete shadow allocation enabled transmitted light");

  std::vector<PortalLightShadowAttachment> overflow(
      PortalLightAttachmentLimit + 1, incomplete);
  mpp::ScenePassOverrides boundedPass;
  require(!AttachPortalLightsToPass(boundedPass, overflow) &&
              boundedPass.uniforms.getNumUniforms() == 0 &&
              boundedPass.samplers.empty(),
          "an over-budget shadow list did not fail atomically to no spill");
}
}  // namespace

int main() {
  try {
    canonicalTransformPreservesThePlayerTorch();
    stableIdentityPreservesLightOutput();
    multiHopCompositionGatesEveryAperture();
    endpointCyclesAndEquivalentLightsAreRejected();
    mutuallyVisibleLoopIsStableAndBounded();
    independentBudgetsUseDeterministicRanking();
    attachmentsFailAtomicallyWithoutCompleteShadows();
    std::cout << "Bounded recursive Portal Player Torch lighting passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
