#include "TriplanarMaterial.h"

#include <utility>

using namespace std;
using namespace wp::application::resourcesystem;

TriplanarMaterial::TriplanarMaterial(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags, ResourceLocation* location)
    : Resource(name, namesp, "TriplanarMaterial", source, tags, location) {}

void TriplanarMaterial::destroy() {
  mAlbedo.reset();
  mTileWidth = defaultTileWidth;
  mBlendSharpness = defaultBlendSharpness;
}

void TriplanarMaterial::initialize(
    shared_ptr<ImageResource> albedo, float tileWidth, float blendSharpness) {
  mAlbedo = move(albedo);
  mTileWidth = tileWidth;
  mBlendSharpness = blendSharpness;
}

shared_ptr<ImageResource> const& TriplanarMaterial::getAlbedo() const {
  return mAlbedo;
}

float TriplanarMaterial::getTileWidth() const { return mTileWidth; }

float TriplanarMaterial::getTileHeight() const {
  if (!mAlbedo || mAlbedo->getWidth() <= 0) return 0.0f;
  return mTileWidth * static_cast<float>(mAlbedo->getHeight()) /
         static_cast<float>(mAlbedo->getWidth());
}

float TriplanarMaterial::getBlendSharpness() const {
  return mBlendSharpness;
}
