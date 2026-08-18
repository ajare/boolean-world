#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include <core/Defines.h>

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
  preview.lloydIterations = 0;
  preview.overlapPercent = 25.0f;
  preview.requestOpen();

  require(preview.open && preview.openRequested,
          "preview did not request its modal");
  require(preview.minimumSpacing == 128.0f && preview.maximumSites == 2000 &&
              preview.seed == 0 && preview.lloydIterations == 5 &&
              preview.overlapPercent == 10.0f,
          "preview did not restore the agreed defaults");

  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}}, 1);
  require(preview.hasCompletePreview(), "valid Rectangle preview generation failed");
  preview.lloydIterations = 6;
  preview.invalidateLayout();
  require(!preview.layout && preview.rectangles.empty() && preview.error.empty(),
          "changing Lloyd iterations did not invalidate the layout");
  preview.close();
  require(!preview.open && !preview.openRequested && !preview.layout &&
              preview.rectangles.empty(),
          "closing the modal retained editor overlay data");
}

void failedGenerationRetainsThePreviousValidPreviewButDisablesPlacement() {
  editor::PrimitiveFieldPreview preview;
  preview.requestOpen();
  preview.minimumSpacing = 64.0f;
  preview.maximumSites = 5;
  preview.seed = 11;
  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}}, 1);
  require(preview.hasCompletePreview(), "valid preview fixture failed");
  auto previousSites = preview.layout->sites;
  auto previousRectangles = preview.rectangles;

  preview.lloydIterations = 21;
  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}}, 1);
  require(preview.layout.has_value() && !preview.error.empty() &&
              !preview.hasCompletePreview(),
          "failed relaxation did not preserve and disable the valid preview");

  preview.lloydIterations = 5;
  preview.minimumSpacing = 1000.0f;
  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}}, 1);
  require(!preview.error.empty(), "failed generation did not expose an error");
  require(preview.layout.has_value() &&
              preview.layout->sites == previousSites &&
              preview.rectangles.size() == previousRectangles.size(),
          "failed generation replaced the previous valid preview");
}

void anglesFittingAndOverlapAreDeterministic() {
  bw::core::PrimitiveFieldLayout layout{
      .worldExtents = {{-10.0f, -8.0f}, {10.0f, 8.0f}},
      .sites = {{0.0f, 0.0f}, {-4.0f, 1.0f}, {5.0f, -2.0f}},
      .cells = {
          {{{-3.0f, -2.0f}, {4.0f, -3.0f}, {3.0f, 4.0f}, {-2.0f, 3.0f}}},
          {{{-10.0f, -8.0f}, {0.0f, -8.0f}, {0.0f, 8.0f}, {-10.0f, 8.0f}}},
          {{{0.0f, -8.0f}, {10.0f, -8.0f}, {10.0f, 8.0f}, {0.0f, 8.0f}}},
      }};

  auto zero = editor::buildPrimitiveFieldRectanglePreview(layout, 0.0f, 73);
  auto repeated = editor::buildPrimitiveFieldRectanglePreview(layout, 0.0f, 73);
  auto overlap = editor::buildPrimitiveFieldRectanglePreview(layout, 25.0f, 73);
  require(zero.succeeded() && repeated.succeeded() && overlap.succeeded(),
          "representative Rectangle fitting failed");
  require(zero.rectangles->size() == layout.sites.size(),
          "not every cell received a Rectangle");

  for (size_t i = 0; i < zero.rectangles->size(); ++i) {
    auto const& rectangle = (*zero.rectangles)[i];
    auto const& repeatedRectangle = (*repeated.rectangles)[i];
    require(rectangle.angle >= 0.0f && rectangle.angle < 360.0f &&
                rectangle.angle == repeatedRectangle.angle &&
                rectangle.size == repeatedRectangle.size,
            "placement angle or fitting was not deterministic");
    require((*overlap.rectangles)[i].size == rectangle.size * 1.25f,
            "positive overlap did not apply the exact post-fit multiplier");

    for (auto const& vertex : layout.cells[i].vertices) {
      auto local = vertex - layout.sites[i];
      local.rotateClockwise(rectangle.angle);
      require(std::abs(local.x) <=
                      rectangle.size * 0.5f +
                          bw::core::PrimitiveFieldNumericTolerance &&
                  std::abs(local.y) <=
                      rectangle.size * 0.5f /
                              editor::PrimitiveFieldRectangleXyRatio +
                          bw::core::PrimitiveFieldNumericTolerance,
              "a zero-overlap fitted Rectangle did not contain its cell");
    }
  }

  require((*zero.rectangles)[0].angle == 70.7228546142578125f &&
              (*zero.rectangles)[1].angle == 135.0145263671875f &&
              (*zero.rectangles)[2].angle == 296.30914306640625f,
          "the fixed placement-angle fixture changed");
}

void overlapValidationAndCapacityAreExplicit() {
  bw::core::PrimitiveFieldLayout layout{
      .worldExtents = {{-1.0f, -1.0f}, {1.0f, 1.0f}},
      .sites = {{0.0f, 0.0f}},
      .cells = {{{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}}}}};
  require(!editor::buildPrimitiveFieldRectanglePreview(layout, -0.01f, 0)
               .succeeded(),
          "negative overlap was accepted");
  require(!editor::buildPrimitiveFieldRectanglePreview(
               layout, std::numeric_limits<float>::infinity(), 0)
               .succeeded(),
          "non-finite overlap was accepted");
  require(!editor::buildPrimitiveFieldRectanglePreview(layout, 100.01f, 0)
               .succeeded(),
          "overlap above 100 percent was accepted");

  require(editor::effectivePrimitiveFieldMaximum(2000, 1) == 2000 &&
              editor::effectivePrimitiveFieldMaximum(
                  2000, BW_WORLD_PRIMITIVE_COUNT_MAX - 7) == 7 &&
              editor::effectivePrimitiveFieldMaximum(
                  2000, BW_WORLD_PRIMITIVE_COUNT_MAX) == 0,
          "effective capacity did not account for authored primitives and ghost");

  editor::PrimitiveFieldPreview preview;
  preview.requestOpen();
  preview.minimumSpacing = 32.0f;
  preview.maximumSites = 20;
  preview.generate(
      {{-128.0f, -128.0f}, {128.0f, 128.0f}},
      BW_WORLD_PRIMITIVE_COUNT_MAX - 2);
  require(preview.hasCompletePreview() && preview.layout->sites.size() == 2,
          "layout generation did not reduce the user batch cap to capacity");
}

}  // namespace

int main() {
  try {
    opensWithDefaultsAndClosesWithoutDocumentData();
    failedGenerationRetainsThePreviousValidPreviewButDisablesPlacement();
    anglesFittingAndOverlapAreDeterministic();
    overlapValidationAndCapacityAreExplicit();
    std::cout << "Primitive-field preview state tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
