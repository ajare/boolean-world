#pragma once

#include <core/Layer.h>

namespace bw::test {
// Test setup uses only the public independent-Portal authoring API.
inline uint32_t addPortalCycle(core::Layer* layer,
                              core::AuthoredAperture first,
                              core::AuthoredAperture second) {
  auto a = layer->addPortal(first);
  auto b = layer->addPortal(second);
  layer->setPortalTarget(a, b);
  layer->setPortalTarget(b, a);
  return a;
}
inline uint32_t insertPortalAfter(core::Layer* layer, uint32_t source,
                                  core::AuthoredAperture aperture) {
  auto target = layer->getPortal(source)->getTargetId();
  auto id = layer->addPortal(aperture);
  layer->setPortalTarget(id, target);
  layer->setPortalTarget(source, id);
  return id;
}
}  // namespace bw::test
