#include <iostream>
#include <stdexcept>
#include <string>

#include "PrimitiveFieldPreview.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void opensWithDefaultsAndClosesWithoutDocumentData() {
  editor::PrimitiveFieldPreview preview;
  preview.minimumSpacing = 9.0f;
  preview.maximumSites = 4;
  preview.seed = 42;
  preview.requestOpen();

  require(preview.open && preview.openRequested,
          "preview did not request its modal");
  require(preview.minimumSpacing == 128.0f && preview.maximumSites == 2000 &&
              preview.seed == 0,
          "preview did not restore the agreed defaults");

  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}});
  require(preview.layout.has_value(), "valid preview generation failed");
  preview.close();
  require(!preview.open && !preview.openRequested && !preview.layout,
          "closing the modal retained editor overlay data");
}

void failedGenerationRetainsThePreviousValidPreview() {
  editor::PrimitiveFieldPreview preview;
  preview.requestOpen();
  preview.minimumSpacing = 64.0f;
  preview.maximumSites = 5;
  preview.seed = 11;
  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}});
  require(preview.layout.has_value(), "valid preview fixture failed");
  auto previousSites = preview.layout->sites;

  preview.minimumSpacing = 1000.0f;
  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}});
  require(!preview.error.empty(), "failed generation did not expose an error");
  require(preview.layout.has_value() &&
              preview.layout->sites.size() == previousSites.size(),
          "failed generation replaced the previous valid preview");
  for (size_t i = 0; i < previousSites.size(); ++i) {
    require(preview.layout->sites[i].x == previousSites[i].x &&
                preview.layout->sites[i].y == previousSites[i].y,
            "failed generation changed the previous valid preview");
  }
}

}  // namespace

int main() {
  try {
    opensWithDefaultsAndClosesWithoutDocumentData();
    failedGenerationRetainsThePreviousValidPreview();
    std::cout << "Primitive-field preview state tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
