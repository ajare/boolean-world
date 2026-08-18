#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <core/CirclePolygon.h>
#include <core/RectanglePolygon.h>
#include <core/RegularPolygon.h>
#include <core/World.h>

#include "Actions.h"
#include "AppHelpers.h"
#include "Document.h"
#include "PrimitiveFieldPlacement.h"
#include "PrimitiveFieldPreview.h"
#include "Settings.h"
#include "Undo.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();
editor::Settings gEditorSettings;

void openTiledPrefabFile(std::string const&, std::shared_ptr<bw::core::World>) {
}

namespace {
int generationRequests = 0;
}

namespace editor {
void generateClipping(Document*, Settings const& settings, int flag) {
  if (settings.configFlags & flag) {
    ++generationRequests;
  }
}
}  // namespace editor

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool sameMaterial(
    bw::core::MaterialDefinitionData const& lhs,
    bw::core::MaterialDefinitionData const& rhs) {
  return lhs.params == rhs.params && lhs.baseColour == rhs.baseColour;
}

bool sameProperties(
    bw::core::PrimitivePropertySet const& lhs,
    bw::core::PrimitivePropertySet const& rhs) {
  return lhs.floorZ == rhs.floorZ && lhs.ceilingZ == rhs.ceilingZ &&
         lhs.floorMaterialIndex == rhs.floorMaterialIndex &&
         lhs.ceilingMaterialIndex == rhs.ceilingMaterialIndex &&
         lhs.wallMaterialIndex == rhs.wallMaterialIndex &&
         sameMaterial(lhs.floorMaterialDef.data, rhs.floorMaterialDef.data) &&
         sameMaterial(lhs.ceilingMaterialDef.data,
                      rhs.ceilingMaterialDef.data) &&
         sameMaterial(lhs.wallMaterialDef.data, rhs.wallMaterialDef.data);
}

bw::core::PrimitiveFieldLayout representativeLayout() {
  auto result = bw::core::buildBoundedPrimitiveFieldLayout(
      {{-100.0f, -80.0f}, {100.0f, 80.0f}},
      {{70.0f, 20.0f}, {-60.0f, -25.0f}, {0.0f, 0.0f}, {-20.0f, 55.0f}, {35.0f, -55.0f}});
  require(result.succeeded(), "representative bounded layout failed");
  return std::move(*result.layout);
}

std::vector<editor::PrimitiveFieldPrimitivePreview> previews(
    bw::core::PrimitiveFieldLayout const& layout,
    float overlap = 10.0f,
    int seed = 1) {
  auto result = editor::buildPrimitiveFieldPreview(
      layout, {}, overlap, seed);
  require(result.succeeded(), "representative primitive preview failed");
  return std::move(*result.primitives);
}

void placementAppendsDefaultsAndIsOneUndoableAction() {
  editor::Document document;
  document.newDoc();
  auto world = document.getWorld();
  world->setName("before placement");
  auto authored = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Difference,
      bw::core::Primitive::FillRule::EvenOdd, 3.0f);
  authored->setPosition({17.0f, -9.0f});
  world->addPrimitive(authored);
  auto ghostBefore = world->getPrimitive(0);
  auto authoredBefore = world->getPrimitive(1);
  document.setSelectedPrimitiveIndices({1});
  document.setModified(false);

  auto layout = representativeLayout();
  auto primitives = previews(layout);
  std::set<editor::PrimitiveFieldType> placedTypes;
  for (auto const& primitive : primitives) {
    placedTypes.insert(primitive.type);
  }
  require(placedTypes.size() == 5,
          "mixed placement fixture did not cover every eligible type");
  auto oldCount = world->getNumPrimitives();
  auto undoBefore = editor::getUndoLevels();
  generationRequests = 0;

  auto result = editor::placePrimitiveField(
      &document, layout, primitives, gEditorSettings);
  require(result.placed && result.error.empty(),
          "valid primitive field placement failed: " + result.error);
  world = document.getWorld();
  require(world->getNumPrimitives() == oldCount + primitives.size(),
          "placement did not append the complete batch");
  require(world->getPrimitive(0) == ghostBefore &&
              world->getPrimitive(1) == authoredBefore &&
              authoredBefore->getPosition() == wp::Vector2{17.0f, -9.0f} &&
              authoredBefore->getOperation() ==
                  bw::core::Primitive::Operation::Difference,
          "successful placement replaced or changed existing primitives");

  std::set<uint32_t> expectedSelection;
  bw::core::RectanglePolygon expected(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      editor::PrimitiveFieldRectangleXyRatio);
  editor::_setPrimitiveParameters(
      &expected, 0, 0, {}, {}, primitives[0].size, primitives[0].angle);
  editor::setPrimitiveDefaultMaterials(&expected);

  for (size_t i = 0; i < primitives.size(); ++i) {
    auto index = oldCount + static_cast<uint32_t>(i);
    expectedSelection.insert(index);
    auto primitive = world->getPrimitive(index);
    require(primitive && primitive->getPosition() == layout.sites[i] &&
                primitive->getSize() ==
                    wp::Vector2{primitives[i].size, primitives[i].size},
            "primitive insertion order, position, or size did not match preview");
    switch (primitives[i].type) {
      case editor::PrimitiveFieldType::Triangle:
        require(dynamic_cast<bw::core::RegularPolygon*>(primitive) &&
                    dynamic_cast<bw::core::RegularPolygon*>(primitive)->getNumSides() == 3,
                "Triangle was not placed as a 3-sided Regular primitive");
        break;
      case editor::PrimitiveFieldType::Pentagon:
        require(dynamic_cast<bw::core::RegularPolygon*>(primitive) &&
                    dynamic_cast<bw::core::RegularPolygon*>(primitive)->getNumSides() == 5,
                "Pentagon was not placed as a 5-sided Regular primitive");
        break;
      case editor::PrimitiveFieldType::Hexagon:
        require(dynamic_cast<bw::core::RegularPolygon*>(primitive) &&
                    dynamic_cast<bw::core::RegularPolygon*>(primitive)->getNumSides() == 6,
                "Hexagon was not placed as a 6-sided Regular primitive");
        break;
      case editor::PrimitiveFieldType::Circle:
        require(dynamic_cast<bw::core::CirclePolygon*>(primitive) &&
                    dynamic_cast<bw::core::CirclePolygon*>(primitive)->getResolution() ==
                        editor::PrimitiveFieldCircleResolution,
                "Circle did not use editor resolution 0.5");
        break;
      case editor::PrimitiveFieldType::Rectangle:
        require(dynamic_cast<bw::core::RectanglePolygon*>(primitive) &&
                    dynamic_cast<bw::core::RectanglePolygon*>(primitive)->getXyRatio() ==
                        editor::PrimitiveFieldRectangleXyRatio,
                "Rectangle did not use the normal editor ratio");
        break;
    }
    require(primitive->getOperation() ==
                    bw::core::Primitive::Operation::Union &&
                primitive->getFillRule() ==
                    bw::core::Primitive::FillRule::NonZero &&
                primitive->getLayer() == 0 && primitive->getPriority() == 0 &&
                primitive->getOrientation() == 0.0f && primitive->isStatic(),
            "a placed primitive did not use normal editor defaults");
    require(sameProperties(primitive->getProperties(), expected.getProperties()),
            "placed primitive materials did not match normal editor creation");

    auto const angle = primitives[i].angle;
    auto const& angleInterpolator = primitive->getAnimationInterpolator(
        bw::core::VertexTransformer::Key::Angle);
    require(angleInterpolator.getPoints() ==
                std::vector<std::pair<float, float>>{
                    {0.0f, angle}, {1.0f, angle}},
            "Angle keyframes did not store the generated angle");
    primitive->resetAnimator(bw::core::VertexTransformer::Key::Angle);
    require(primitive->getAnimationInterpolator(
                         bw::core::VertexTransformer::Key::Angle)
                    .getPoints() ==
                std::vector<std::pair<float, float>>{
                    {0.0f, angle}, {1.0f, angle}},
            "Angle default animation structure did not store the angle");

    primitive->calculateAnimationValues();
    primitive->updateVertexPositions();
    auto const& transformed = primitive->getVertices();
    require(transformed.size() == 1 && transformed[0].size() == 1 &&
                transformed[0][0].size() == primitives[i].contour.size(),
            "placed Rectangle did not produce one complete contour");
    for (size_t vertex = 0; vertex < primitives[i].contour.size(); ++vertex) {
      auto delta = transformed[0][0][vertex].p - primitives[i].contour[vertex];
      require(std::abs(delta.x) <=
                      bw::core::PrimitiveFieldNumericTolerance &&
                  std::abs(delta.y) <=
                      bw::core::PrimitiveFieldNumericTolerance,
              "placed Rectangle contour did not match the visible preview");
    }
  }

  require(document.getSelectedPrimitiveIndices() == expectedSelection &&
              document.isModified(),
          "placement did not select exactly the generated batch and mark dirty");
  require(editor::getUndoLevels() == undoBefore + 1,
          "batch placement did not create exactly one undo entry");
  require(generationRequests == 1,
          "placement did not request generation exactly once");

  auto placedAngles = std::vector<float>{};
  for (auto index : expectedSelection) {
    placedAngles.push_back(document.getWorld()
                               ->getPrimitive(index)
                               ->getAnimationInterpolator(
                                   bw::core::VertexTransformer::Key::Angle)
                               .getValue(0.0f));
  }

  editor::undo(&document);
  require(document.getWorld()->getNumPrimitives() == oldCount &&
              document.getWorld()->getName() == "before placement" &&
              document.getSelectedPrimitiveIndices() == std::set<uint32_t>{1} &&
              !document.isModified() && generationRequests == 2,
          "Undo did not restore world, selection, dirty state, and one generation");

  editor::redo(&document);
  require(document.getWorld()->getNumPrimitives() ==
                  oldCount + primitives.size() &&
              document.getSelectedPrimitiveIndices() == expectedSelection &&
              document.isModified() && generationRequests == 3,
          "Redo did not restore the generated batch and one generation");
  for (size_t i = 0; i < placedAngles.size(); ++i) {
    require(document.getWorld()
                    ->getPrimitive(oldCount + static_cast<uint32_t>(i))
                    ->getAnimationInterpolator(
                        bw::core::VertexTransformer::Key::Angle)
                    .getValue(0.0f) == placedAngles[i],
            "Redo did not restore the identical primitive field");
  }
}

void insertionFailureIsAtomicAndDoesNotTouchHistory() {
  editor::Document document;
  document.newDoc();
  document.getWorld()->setName("atomic fixture");
  document.setSelectedPrimitiveIndices({0});
  document.setModified(true);
  auto layout = representativeLayout();
  auto primitives = previews(layout, 0.0f, 4);
  auto oldCount = document.getWorld()->getNumPrimitives();
  auto undoBefore = editor::getUndoLevels();
  auto redoBefore = editor::getRedoLevels();
  int insertions = 0;
  generationRequests = 0;

  auto result = editor::placePrimitiveField(
      &document, layout, primitives, gEditorSettings,
      [&](bw::core::World& world, bw::core::Primitive* primitive) {
        if (++insertions == 2) {
          throw std::runtime_error("injected insertion failure");
        }
        return world.addPrimitive(primitive);
      });

  require(!result.placed && result.error.find("injected insertion failure") !=
                                std::string::npos,
          "injected insertion failure was not reported");
  require(document.getWorld()->getNumPrimitives() == oldCount &&
              document.getWorld()->getName() == "atomic fixture" &&
              document.getSelectedPrimitiveIndices() == std::set<uint32_t>{0} &&
              document.isModified() &&
              editor::getUndoLevels() == undoBefore &&
              editor::getRedoLevels() == redoBefore &&
              generationRequests == 0,
          "insertion failure left document, history, or generation side effects");
}

void invalidBatchFailsBeforeInsertionAndCapacitySettingIsHonoured() {
  editor::Document document;
  document.newDoc();
  document.setSelectedPrimitiveIndices({0});
  auto layout = representativeLayout();
  auto primitives = previews(layout);
  primitives.back().angle = std::numeric_limits<float>::quiet_NaN();
  int insertionCalls = 0;
  auto undoBefore = editor::getUndoLevels();

  auto result = editor::placePrimitiveField(
      &document, layout, primitives, gEditorSettings,
      [&](bw::core::World& world, bw::core::Primitive* primitive) {
        ++insertionCalls;
        return world.addPrimitive(primitive);
      });
  require(!result.placed && insertionCalls == 0 &&
              document.getWorld()->getNumPrimitives() == 1 &&
              document.getSelectedPrimitiveIndices() == std::set<uint32_t>{0} &&
              editor::getUndoLevels() == undoBefore,
          "invalid batch mutated the document before complete validation");

  auto validPrimitives = previews(layout);
  editor::Settings noCreateGeneration = gEditorSettings;
  noCreateGeneration.configFlags &= ~ED_CLIP_ON_PRIM_CREATE_DELETE;
  generationRequests = 0;
  result = editor::placePrimitiveField(
      &document, layout, validPrimitives, noCreateGeneration);
  require(result.placed && generationRequests == 0,
          "disabled create/delete generation policy was not honoured");
}

}  // namespace

int main() {
  try {
    placementAppendsDefaultsAndIsOneUndoableAction();
    insertionFailureIsAtomicAndDoesNotTouchHistory();
    invalidBatchFailsBeforeInsertionAndCapacitySettingIsHonoured();
    std::cout << "Primitive-field placement action tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
