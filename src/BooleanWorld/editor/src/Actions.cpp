#include <algorithm>

#include <core/RegularPolygon.h>
#include <core/CirclePolygon.h>
#include <core/TorusPolygon.h>
#include <core/RectanglePolygon.h>
#include <core/SuperformulaPolygon.h>
#include <core/MeshPrimitive.h>
#include <core/PrimitiveField.h>

#include "Defines.h"
#include "Actions.h"
#include "EditorException.h"

namespace editor {
using namespace std;

bool recordCurrentState(Document* doc, bool modifying) {
  return modifying;
}

void setEditorMode(Document* doc, Settings& settings, Settings::Mode mode) {
  if (settings.mode == mode) {
    return;
  }

  if (mode == Settings::Mode::Mesh && doc->isActive()) {
    auto const& selection = doc->getSelectedPrimitiveIndices();
    if (selection.size() == 1 && doc->activateMesh(*selection.begin())) {
      settings.activeMeshPrimitiveIndex = *selection.begin();
    } else {
      doc->clearActiveMesh();
      settings.activeMeshPrimitiveIndex = ~0u;
    }
  } else {
    doc->clearActiveMesh();
    settings.activeMeshPrimitiveIndex = ~0u;
  }

  settings.mode = mode;
  doc->clearSelections();
}

void setMeshSubMode(
    Document* doc, Settings& settings, Settings::MeshSubMode subMode) {
  if (settings.meshSubMode == subMode) {
    return;
  }

  settings.meshSubMode = subMode;
  doc->clearSelections();
}

bool setWorldName(Document* doc, string const& name) {
  auto world = doc->getWorld();

  world->setName(name);
  return true;
}

bool addLayer(Document* doc, string const& name) {
  auto world = doc->getWorld();

  world->addLayer(name);
  return true;
}

bool setLayerBuildStepEnabled(Document* doc, bw::core::Layer* layer, uint32_t stepIndex, bool enabled) {
  layer->setStepEnabled(stepIndex, enabled);
  return true;
}

bool addLayerBuildStep(Document* doc, bw::core::Layer* layer) {
  layer->addStep(new bw::core::PrimitiveField());
  return true;
}

bool removeLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t stepIndex) {
  layer->removeStep(stepIndex);
  return true;
}

bool moveLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t fromIndex, uint32_t toIndex) {
  layer->moveStep(fromIndex, toIndex);
  return true;
}

// World::movePrimitiveToLayer lands primitive in destinationLayer's first
// (PrimitiveField) step via Layer::addPrimitive, and releases it from
// whichever step of its source Layer produced it. Every Layer this World
// owns is serialized inline (docs/adr/0013), so the transactUndoableAction
// this is wired into snapshots and restores both the source and destination
// Layers' resulting Primitives exactly.
bool movePrimitiveToLayer(Document* doc, bw::core::Primitive* primitive, bw::core::Layer* destinationLayer) {
  auto world = doc->getWorld();

  world->movePrimitiveToLayer(primitive, destinationLayer);
  return true;
}

bool moveTriggerLineToLayer(Document* doc, bw::core::WorldTriggerLine* triggerLine, bw::core::Layer* destinationLayer) {
  auto world = doc->getWorld();

  world->moveTriggerLineToLayer(triggerLine, destinationLayer);
  return true;
}

bool setWorldDescription(Document* doc, string const& desc) {
  auto world = doc->getWorld();

  world->setDescription(desc);
  return true;
}

bool setPlayerStartPosition(Document* doc, wp::Vector2 const& pos) {
  auto world = doc->getWorld();

  world->setPlayerStartPosition(pos);
  return true;
}

bool setPlayerStartAngle(Document* doc, float angle) {
  auto world = doc->getWorld();

  world->setPlayerStartAngle(angle);
  return true;
}

bool selectWorldVertex(Document* doc, uint32_t worldVertexIndex) {
  doc->setSelectedWorldVertexIndex(worldVertexIndex);
  return false;
}

bool selectTriggerLine(Document* doc, uint32_t triggerLineIndex) {
  doc->setSelectedTriggerLineIndex(triggerLineIndex);
  return false;
}

bool deleteTriggerLine(Document* doc, uint32_t triggerLineIndex) {
  doc->getWorld()->removeTriggerLine(triggerLineIndex);
  return true;
}

bool selectPrimitive(Document* doc, uint32_t primitiveIndex) {
  doc->setSelectedPrimitiveIndices({primitiveIndex});
  return false;
}

bool togglePrimitiveSelected(Document* doc, uint32_t primitiveIndex) {
  if (doc->indexInSelection(primitiveIndex)) {
    doc->removeSelectedPrimitiveIndex(primitiveIndex);
  } else {
    doc->addSelectedPrimitiveIndex(primitiveIndex);
  }

  return false;
}

bool selectPrimitives(Document* doc, set<uint32_t> const& primitiveIndices) {
  doc->setSelectedPrimitiveIndices(primitiveIndices);
  return false;
}

bool addPrimitivesToSelection(Document* doc, set<uint32_t> const& primitiveIndices) {
  doc->addSelectedPrimitiveIndices(primitiveIndices);
  return false;
}

bool togglePrimitivesSelected(Document* doc, set<uint32_t> const& primitiveIndices) {
  for (auto primitiveIndex : primitiveIndices) {
    if (doc->indexInSelection(primitiveIndex)) {
      doc->removeSelectedPrimitiveIndex(primitiveIndex);
    } else {
      doc->addSelectedPrimitiveIndex(primitiveIndex);
    }
  }

  return false;
}

bool clearSelections(Document* doc) {
  doc->clearSelections();

  return false;
}

bool createPrimitiveFromGhost(Document* doc) {
  auto world = doc->getWorld();
  auto* activeStep = world->getActiveLayer()->getActiveStep();
  if (!activeStep->acceptsNewPrimitives()) {
    return false;
  }

  auto ghost = doc->getGhost();
  auto prim = ghost->copy();

  // Clear the editor-only ghost flag.
  prim->setFlags(prim->getFlags() & ~BW_PRIMITIVE_GHOST_FLAG);

  // copy() carries mWorld over from ghost (already added to world), so prim
  // must be added to a Layer before anything on it can trigger
  // notifyWorldChanged - otherwise World::primitiveChanged's scan for its
  // owning Layer finds none and throws.
  world->addPrimitive(prim);

  // Set material defaults
  setPrimitiveDefaultMaterials(prim);

  return true;
}

bool clonePrimitive(Document* doc, uint32_t primitiveIndex) {
  auto world = doc->getWorld();
  if (!world->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
    return false;
  }

  auto primitive = world->getPrimitive(primitiveIndex);

  world->addPrimitive(primitive->copy());
  return true;
}

bool cloneRotatedPrimitive(Document* doc, uint32_t primitiveIndex, float angle) {
  auto world = doc->getWorld();
  if (!world->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
    return false;
  }

  auto primitive = world->getPrimitive(primitiveIndex);

  world->addPrimitive(primitive->rotatedCopy(angle));
  return true;
}

bool deletePrimitives(Document* doc, set<uint32_t> const& primitiveIndices) {
  vector<uint32_t> vec(primitiveIndices.begin(), primitiveIndices.end());
  doc->getWorld()->removePrimitives(vec);
  return true;
}

bool bakePrimitives(Document* doc, set<uint32_t> const& primitiveIndices) {
  vector<uint32_t> vec(primitiveIndices.begin(), primitiveIndices.end());
  auto index = doc->getWorld()->convertPrimitivesToMesh(vec);

  if (index != ~0u) {
    doc->setSelectedPrimitiveIndices({index});
    return true;
  } else {
    return false;
  }
}

bool clipPrimitivesToGrid(Document* doc, set<uint32_t> const& primitiveIndices, float gridSize) {
  vector<uint32_t> vec(primitiveIndices.begin(), primitiveIndices.end());

  auto world = doc->getWorld();

  // Create a mesh primitive to clip
  auto meshTemplateIndex = world->convertPrimitivesToMesh(vec);

  if (meshTemplateIndex == ~0u) {
    return false;
  }

  auto meshTemplate = world->getPrimitive(meshTemplateIndex);

  // Get all grid cells
  auto templateBounds = meshTemplate->getBounds();
  templateBounds.expandToGrid(wp::Vector2(gridSize, gridSize));

  wp::Vector2 minExtent, maxExtent;
  templateBounds.getExtents(minExtent, maxExtent);

  int dx = (int)((maxExtent.x - minExtent.x) / gridSize);
  int dy = (int)((maxExtent.y - minExtent.y) / gridSize);

  vector<bw::core::Primitive*> createdPrimitives;

  for (int y = 0; y < dy; y++) {
    for (int x = 0; x < dx; x++) {
      wp::Vector2 cellMin{minExtent.x + x * gridSize, minExtent.y + y * gridSize};
      wp::Vector2 cellMax = cellMin + gridSize;
      bw::core::RectanglePolygon cell(
          bw::core::Primitive::Operation::Intersection,
          bw::core::Primitive::FillRule::EvenOdd,
          1.0f);
      cell.setPosition((cellMin + cellMax) * 0.5f);
      cell.setSize(gridSize, gridSize);
      cell.setPriority(BW_PRIORITY_MAX_VALUE);
      cell.updateVertexPositions();

      auto cellMesh = world->createMeshPrimitive({meshTemplate, &cell});
      if (cellMesh) {
        createdPrimitives.push_back(cellMesh);
      }
    }
  }

  // Delete temporary mesh primitive
  world->removePrimitive(meshTemplateIndex);

  for (auto p : createdPrimitives) {
    world->addPrimitive(p);
  }

  return true;
}

bool setPrimitiveOperation(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::Operation op) {
  primitive->setOperation(op);
  return true;
}

bool setPrimitiveFillRule(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::FillRule fillRule) {
  primitive->setFillRule(fillRule);
  return true;
}

bool setPrimitiveOrientation(Document* doc, bw::core::Primitive* primitive, float orient) {
  primitive->setOrientation(orient);
  return true;
}

bool setPrimitiveSize(Document* doc, bw::core::Primitive* primitive, float size) {
  primitive->setSize(size, size);
  return true;
}

bool setPrimitivePosition(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& position) {
  primitive->setPosition(position);

  // Update vertices for visual purposes
  primitive->updateVertexPositions();
  return true;
}

bool setPrimitiveTransformOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& transformOrigin) {
  primitive->setTransformOffset(transformOrigin);

  // Update vertices for visual purposes
  primitive->updateVertexPositions();
  return true;
}

bool setPrimitiveInfluenceOriginOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& influenceOriginOffset) {
  primitive->setInfluenceEyeOriginOffset(influenceOriginOffset);
  return true;
}

bool setPrimitiveFollowOrbitAngle(Document* doc, bw::core::Primitive* primitive, bool orient) {
  primitive->setFollowOrbitAngle(orient);
  return true;
}

bool setPrimitivePriority(Document* doc, bw::core::Primitive* primitive, uint8_t priority) {
  primitive->setPriority(priority);
  return true;
}

bool increasePrimitivePriority(Document* doc, bw::core::Primitive* primitive) {
  int priority = (int)primitive->getPriority();
  int newPriority = min(255, priority + 1);

  primitive->setPriority((uint8_t)newPriority);
  return true;
}

bool decreasePrimitivePriority(Document* doc, bw::core::Primitive* primitive) {
  int priority = (int)primitive->getPriority();
  int newPriority = max(0, priority - 1);

  primitive->setPriority((uint8_t)newPriority);
  return true;
}

bool setPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value) {
  primitive->updateAnimatedPropertyEvent(key, index, eventType, triggerType, value);
  return true;
}

bool deletePrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  primitive->removeAnimatedPropertyEvent(key, index);
  return true;
}

bool addKeyToInterpolator(Document* doc, string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).addPoint(time, value);
    } else {
      mutation.influence(key).addPoint(time, value);
    }
  }

  return true;
}

bool removeKeyFromInterpolator(Document* doc, string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).removePoint(index);
    } else {
      mutation.influence(key).removePoint(index);
    }
  }

  return true;
}

bool addAnimationKeyToPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value) {
  {
    auto mutation = primitive->mutate();
    mutation.animation(key).addPoint(time, value);
  }
  return true;
}

bool removeAnimationKeyFromPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  {
    auto mutation = primitive->mutate();
    mutation.animation(key).removePoint(index);
  }
  return true;
}

bool updateAnimationKeyInInterpolator(Document* doc, string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, float time, float value) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).updatePoint(index, time, value);
    } else {
      mutation.influence(key).updatePoint(index, time, value);
    }
  }

  return true;
}

bool setInterpolatorEasing(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, bw::core::Easing easing) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).setEasing(index, easing);
    } else {
      mutation.influence(key).setEasing(index, easing);
    }
  }

  return true;
}

bool addTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  auto newTransform = bw::core::tTransform::makePassthroughPrevious();
  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      primitive->addScaleTransform(newTransform);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      primitive->addAngleTransform(newTransform);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      primitive->addOrbitAngleTransform(newTransform);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      primitive->addOrbitDistanceTransform(newTransform);
      break;
  }

  return true;
}

bool removeTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      primitive->removeScaleTransform(index);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      primitive->removeAngleTransform(index);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      primitive->removeOrbitAngleTransform(index);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      primitive->removeOrbitDistanceTransform(index);
      break;
  }

  return true;
}

bool swapTransforms(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index1, uint32_t index2) {
  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      primitive->swapScaleTransforms(index1, index2);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      primitive->swapAngleTransforms(index1, index2);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      primitive->swapOrbitAngleTransforms(index1, index2);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      primitive->swapOrbitDistanceTransforms(index1, index2);
      break;
  }

  return true;
}

bool setTransformOperand(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t operandIndex, bw::core::tTransform::OperandType operand) {
  primitive->setTransformOperand(key, transformIndex, operandIndex, operand);
  return true;
}

bool setTransformInput(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t inputIndex, bw::core::InputType input) {
  primitive->setTransformInput(key, transformIndex, inputIndex, input);
  return true;
}

bool setTransformConstant(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t constantIndex, float constant) {
  primitive->setTransformConstant(key, transformIndex, constantIndex, constant);
  return true;
}

bool setTransformFnMultiplier(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t fnMulIndex, float value) {
  primitive->setTransformFnMultiplier(key, transformIndex, fnMulIndex, value);
  return true;
}

bool setTransformTriggerLine(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t indexIndex, uint32_t index) {
  primitive->setTransformTriggerLineIndex(key, transformIndex, indexIndex, index);
  return true;
}

bool setTransformOperation(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, bw::core::tTransform::Operation operation) {
  primitive->setTransformOperation(key, transformIndex, operation);
  return true;
}

}  // namespace editor