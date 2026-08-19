#include <algorithm>

#include <core/RegularPolygon.h>
#include <core/CirclePolygon.h>
#include <core/TorusPolygon.h>
#include <core/RectanglePolygon.h>
#include <core/SuperformulaPolygon.h>
#include <core/MeshPrimitive.h>

#include "Defines.h"
#include "Actions.h"
#include "UiHelpers.h"
#include "EditorException.h"

extern editor::Settings gEditorSettings;

namespace editor {
using namespace std;

bool recordCurrentState(Document* doc, bool modifying) {
  return modifying;
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

// Wired into the same transactUndoableAction/undo/redo mechanism as every
// other Action. Document's Undo snapshot round-trips a World through
// serialize/deserialize, which currently only carries the active Layer's
// content (#164 gives every Layer inline representation); until then, an
// undo/redo spanning this move won't fully restore a destination Layer that
// only the move itself populated. This is a pre-existing limitation of every
// Layer-affecting Action since #159/#160, not something this move introduces.
bool movePrimitiveToLayer(Document* doc, bw::core::Primitive* primitive, bw::core::Layer* destinationLayer) {
  auto world = doc->getWorld();

  world->movePrimitiveToLayer(primitive, destinationLayer);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

bool clearSelections(Document* doc) {
  doc->clearSelections();

  return false;
}

bool createPrimitiveFromGhost(Document* doc) {
  auto world = doc->getWorld();
  auto ghost = doc->getGhost();
  auto prim = ghost->copy();

  // Clear the editor-only ghost flag.
  prim->setFlags(prim->getFlags() & ~BW_PRIMITIVE_GHOST_FLAG);

  // Set material defaults
  setPrimitiveDefaultMaterials(prim);

  world->addPrimitive(prim);
  return true;
}

bool clonePrimitive(Document* doc, uint32_t primitiveIndex) {
  auto world = doc->getWorld();

  auto primitive = world->getPrimitive(primitiveIndex);

  world->addPrimitive(primitive->copy());
  return true;
}

bool cloneRotatedPrimitive(Document* doc, uint32_t primitiveIndex, float angle) {
  auto world = doc->getWorld();

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
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setPrimitiveFillRule(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::FillRule fillRule) {
  primitive->setFillRule(fillRule);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setPrimitiveOrientation(Document* doc, bw::core::Primitive* primitive, float orient) {
  primitive->setOrientation(orient);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setPrimitiveSize(Document* doc, bw::core::Primitive* primitive, float size) {
  primitive->setSize(size, size);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setPrimitivePosition(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& position) {
  primitive->setPosition(position);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setPrimitiveTransformOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& transformOrigin) {
  primitive->setTransformOffset(transformOrigin);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool increasePrimitivePriority(Document* doc, bw::core::Primitive* primitive) {
  int priority = (int)primitive->getPriority();
  int newPriority = min(255, priority + 1);

  primitive->setPriority((uint8_t)newPriority);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool decreasePrimitivePriority(Document* doc, bw::core::Primitive* primitive) {
  int priority = (int)primitive->getPriority();
  int newPriority = max(0, priority - 1);

  primitive->setPriority((uint8_t)newPriority);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value) {
  primitive->updateAnimatedPropertyEvent(key, index, eventType, triggerType, value);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool deletePrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  primitive->removeAnimatedPropertyEvent(key, index);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool addAnimationKeyToPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value) {
  {
    auto mutation = primitive->mutate();
    mutation.animation(key).addPoint(time, value);
  }
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool removeAnimationKeyFromPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  {
    auto mutation = primitive->mutate();
    mutation.animation(key).removePoint(index);
  }
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setTransformOperand(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t operandIndex, bw::core::tTransform::OperandType operand) {
  primitive->setTransformOperand(key, transformIndex, operandIndex, operand);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setTransformInput(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t inputIndex, bw::core::InputType input) {
  primitive->setTransformInput(key, transformIndex, inputIndex, input);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setTransformConstant(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t constantIndex, float constant) {
  primitive->setTransformConstant(key, transformIndex, constantIndex, constant);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setTransformFnMultiplier(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t fnMulIndex, float value) {
  primitive->setTransformFnMultiplier(key, transformIndex, fnMulIndex, value);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setTransformTriggerLine(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t indexIndex, uint32_t index) {
  primitive->setTransformTriggerLineIndex(key, transformIndex, indexIndex, index);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

bool setTransformOperation(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, bw::core::tTransform::Operation operation) {
  primitive->setTransformOperation(key, transformIndex, operation);
  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
  return true;
}

}  // namespace editor