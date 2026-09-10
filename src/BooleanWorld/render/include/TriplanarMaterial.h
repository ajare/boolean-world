#pragma once

#include <map>
#include <memory>
#include <string>

#include <willpower/application/resourcesystem/ImageResource.h>
#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include "Platform.h"

// A file-authored image-backed Surface material. The compatible world Programs
// are engine-owned; this Resource owns only its albedo ImageResource and the
// two bounded projection controls described by ADR-0046.
class TriplanarMaterial : public wp::application::resourcesystem::Resource {
  std::shared_ptr<wp::application::resourcesystem::ImageResource> mAlbedo;
  float mTileWidth{32.0f};
  float mBlendSharpness{4.0f};

  void destroy() override;

public:
  static constexpr float minimumTileWidth = 0.25f;
  static constexpr float maximumTileWidth = 1024.0f;
  static constexpr float defaultTileWidth = 32.0f;
  static constexpr float minimumBlendSharpness = 1.0f;
  static constexpr float maximumBlendSharpness = 16.0f;
  static constexpr float defaultBlendSharpness = 4.0f;

  TriplanarMaterial(
      std::string const& name, std::string const& namesp,
      std::string const& source,
      std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location);

  void initialize(
      std::shared_ptr<wp::application::resourcesystem::ImageResource> albedo,
      float tileWidth, float blendSharpness);

  [[nodiscard]] std::shared_ptr<
      wp::application::resourcesystem::ImageResource> const&
  getAlbedo() const;
  [[nodiscard]] float getTileWidth() const;
  [[nodiscard]] float getTileHeight() const;
  [[nodiscard]] float getBlendSharpness() const;
};

class TriplanarMaterialResourceFactory final
    : public wp::application::resourcesystem::ResourceFactory {
public:
  TriplanarMaterialResourceFactory()
      : ResourceFactory("TriplanarMaterial") {}

  wp::application::resourcesystem::Resource* createResource(
      std::string const& name, std::string const& namesp,
      std::string const& source,
      std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location) override {
    return new TriplanarMaterial(name, namesp, source, tags, location);
  }
};
