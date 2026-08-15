#pragma once

#include <cstdint>
#include <set>

#include "Undo.h"
#include "Document.h"


namespace editor
{
	bool recordCurrentState(Document* doc, bool modifying);

	bool setWorldName(Document* doc, std::string const& name);

	bool setWorldDescription(Document* doc, std::string const& desc);
	
	bool setPlayerStartPosition(Document* doc, wp::Vector2 const& pos);

	bool setPlayerStartAngle(Document* doc, float angle);

	bool selectWorldVertex(Document* doc, uint32_t worldVertexIndex);

	bool selectTriggerLine(Document* doc, uint32_t triggerLineIndex);

	bool deleteTriggerLine(Document* doc, uint32_t triggerLineIndex);

	bool selectPrimitive(Document* doc, uint32_t primitiveIndex);

	bool togglePrimitiveSelected(Document* doc, uint32_t primitiveIndex);

	bool clearSelections(Document* doc);

	bool createPrimitiveFromGhost(Document* doc);

	bool clonePrimitive(Document* doc, uint32_t primitiveIndex);

	bool cloneRotatedPrimitive(Document* doc, uint32_t primitiveIndex, float angle);

	bool deletePrimitives(Document* doc, std::set<uint32_t> const& primitiveIndices);

	bool bakePrimitives(Document* doc, std::set<uint32_t> const& primitiveIndices);

	bool clipPrimitivesToGrid(Document* doc, std::set<uint32_t> const& primitiveIndices, float gridSize);

	bool setPrimitiveOperation(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::Operation op);

	bool setPrimitiveFillRule(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::FillRule fillRule);

	bool setPrimitiveOrientation(Document* doc, bw::core::Primitive* primitive, float orient);

	bool setPrimitiveSize(Document* doc, bw::core::Primitive* primitive, float size);

	bool setPrimitivePosition(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& position);

	bool setPrimitiveTransformOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& transformOrigin);

	bool setPrimitiveInfluenceOriginOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& influenceOriginOffset);

	bool setPrimitiveFollowOrbitAngle(Document* doc, bw::core::Primitive* primitive, bool orient);

	bool setPrimitiveLayer(Document* doc, bw::core::Primitive* primitive, uint8_t layer);

	bool setPrimitivePriority(Document* doc, bw::core::Primitive* primitive, uint8_t priority);

	void setPrimitiveDefaultMaterial(uint32_t materialIndex, bw::core::MaterialDefinitionData* materialDefinition);

	void setPrimitiveDefaultMaterials(bw::core::Primitive* prim);

	bool increasePrimitivePriority(Document* doc, bw::core::Primitive* primitive);

	bool decreasePrimitivePriority(Document* doc, bw::core::Primitive* primitive);

	bool setPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value);

	bool deletePrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

	bool addAnimationKeyToPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value);

	bool removeAnimationKeyFromPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

	bool addKeyToInterpolator(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value);

	bool removeKeyFromInterpolator(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

	bool updateAnimationKeyInInterpolator(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, float time, float value);

	bool setInterpolatorEasing(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, bw::core::Easing easing);

	bool addTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key);

	bool removeTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

	bool swapTransforms(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index1, uint32_t index2);

	bool setTransformOperand(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t operandIndex, bw::core::tTransform::OperandType operand);

	bool setTransformInput(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t inputIndex, bw::core::InputType input);

	bool setTransformConstant(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t constantIndex, float constant);

	bool setTransformFnMultiplier(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t fnMulIndex, float value);

	bool setTransformTriggerLine(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t indexIndex, uint32_t index);

	bool setTransformOperation(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, bw::core::tTransform::Operation operation);

} // editor