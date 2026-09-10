#include "TriplanarMaterialResourceDefinitionFactory.h"

#include <cmath>
#include <stdexcept>
#include <string>

#include <willpower/application/resourcesystem/ImageResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "TriplanarMaterial.h"

using namespace std;
using namespace wp;
using namespace wp::application::resourcesystem;

namespace {
float parseBoundedFloat(
    Resource* resource, DataNode* node, char const* field, float defaultValue,
    float minimum, float maximum) {
  auto* valueNode = node->getOptionalChild(field);
  if (!valueNode) return defaultValue;

  auto const text = valueNode->getValue();
  try {
    size_t consumed = 0;
    auto value = stof(text, &consumed);
    if (consumed != text.size() || !isfinite(value) || value < minimum ||
        value > maximum) {
      throw invalid_argument("out of range");
    }
    return value;
  } catch (exception const&) {
    throw ResourceException(
        resource, string(field) + " must be finite and in the inclusive range " +
                      to_string(minimum) + " to " + to_string(maximum) + ".");
  }
}
}  // namespace

TriplanarMaterialResourceDefinitionFactory::
    TriplanarMaterialResourceDefinitionFactory()
    : ResourceDefinitionFactory("TriplanarMaterial", "") {}

void TriplanarMaterialResourceDefinitionFactory::create(
    Resource* resource, ResourceManager*, DataNode* node) {
  node->requireOnlyChildren({"Albedo", "TileWidth", "BlendSharpness"});
  auto* material = static_cast<TriplanarMaterial*>(resource);

  auto* albedoNode = node->getOptionalChild("Albedo");
  if (!albedoNode || albedoNode->getValue().empty()) {
    throw ResourceException(
        resource, "Triplanar material requires an Albedo ImageResource dependency.");
  }

  auto dependency = material->getDependentResource(albedoNode->getValue());
  auto albedo = dynamic_pointer_cast<ImageResource>(dependency);
  if (!albedo) {
    throw ResourceException(
        resource, "Triplanar Albedo dependency '" + albedoNode->getValue() +
                      "' is not an ImageResource.");
  }
  if (albedo->getWidth() <= 0 || albedo->getHeight() <= 0 ||
      (albedo->getNumChannels() != 3 && albedo->getNumChannels() != 4)) {
    throw ResourceException(
        resource, "Triplanar Albedo ImageResource '" +
                      albedo->getQualifiedName() +
                      "' has unsupported dimensions or channels.");
  }
  auto const wrapping = albedo->getTags().find("wrapping");
  if (wrapping == albedo->getTags().end() || wrapping->second != "repeat") {
    throw ResourceException(
        resource, "Triplanar Albedo ImageResource '" +
                      albedo->getQualifiedName() +
                      "' must use repeat wrapping.");
  }

  auto tileWidth = parseBoundedFloat(
      resource, node, "TileWidth", TriplanarMaterial::defaultTileWidth,
      TriplanarMaterial::minimumTileWidth,
      TriplanarMaterial::maximumTileWidth);
  auto blendSharpness = parseBoundedFloat(
      resource, node, "BlendSharpness",
      TriplanarMaterial::defaultBlendSharpness,
      TriplanarMaterial::minimumBlendSharpness,
      TriplanarMaterial::maximumBlendSharpness);
  material->initialize(move(albedo), tileWidth, blendSharpness);
}
