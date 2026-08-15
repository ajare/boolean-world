#pragma once

#include <functional>

#include "core/Primitive.h"

#include "Document.h"


namespace editor
{
	typedef std::function<bw::core::Primitive* ()> CreatePrimitiveFunction;

	typedef std::function<void(Document*)> DocumentHelperFunction;

	typedef std::function<bw::core::Primitive* ()> CreatePrimitiveFunction;

	void newDocument(editor::Document* doc);

	void openDocument(editor::Document* doc);

	void saveDocumentAs(editor::Document* doc);

	void saveDocument(editor::Document* doc);

	void exitApp(editor::Document* doc);

	void showHelp(editor::Document* doc);

	void checkModifiedOperation(editor::Document* doc, std::string const& title, DocumentHelperFunction func);

	void handleModifiedDocument(editor::Document* doc, bool docAction, bool checkDocumentModified, std::string const& docText, DocumentHelperFunction helperFunc);

	void handleNonDocumentAction(std::string const& action);

	void checkNonDocumentOperation();

	void renderHelp();

	bw::core::World* loadWorld(std::string const& filepath);

	void goHome(bw::core::Primitive const* primitive = nullptr);

	void enableGhost(editor::Document* doc, bool enable);

	void selectAndHomeGhost(editor::Document* doc);

	void _setPrimitiveParameters(bw::core::Primitive* prim, uint8_t layer, uint8_t priority, wp::Vector2 const& position, wp::Vector2 const& offset, float scale, float angle);

	bw::core::Primitive* createRegularPolygonPrimitive(
		bw::core::Primitive::Operation op,
		bw::core::Primitive::FillRule fillRule,
		uint32_t numSides,
		uint8_t layer,
		uint8_t priority,
		wp::Vector2 const& position,
		float scale,
		float angle);

	bw::core::Primitive* createCirclePrimitive(
		bw::core::Primitive::Operation op,
		bw::core::Primitive::FillRule fillRule,
		float resolution,
		uint8_t layer,
		uint8_t priority,
		wp::Vector2 const& position,
		float scale,
		float angle);

	bw::core::Primitive* createCircleSegmentPrimitive(
		bw::core::Primitive::Operation op,
		bw::core::Primitive::FillRule fillRule,
		float arcLength,
		float resolution,
		uint8_t layer,
		uint8_t priority,
		wp::Vector2 const& position,
		float scale,
		float angle);

	bw::core::Primitive* createTorusPrimitive(
		bw::core::Primitive::Operation op,
		bw::core::Primitive::FillRule fillRule,
		float thickness,
		float resolution,
		uint8_t layer,
		uint8_t priority,
		wp::Vector2 const& position,
		float scale,
		float angle);

	bw::core::Primitive* createTorusSegmentPrimitive(
		bw::core::Primitive::Operation op,
		bw::core::Primitive::FillRule fillRule,
		float thickness,
		float arcLength,
		float resolution,
		uint8_t layer,
		uint8_t priority,
		wp::Vector2 const& position,
		float scale,
		float angle);

	bw::core::Primitive* createRectanglePrimitive(
		bw::core::Primitive::Operation op,
		bw::core::Primitive::FillRule fillRule,
		float xyRatio,
		uint8_t layer,
		uint8_t priority,
		wp::Vector2 const& position,
		float scale,
		float angle);

	bw::core::Primitive* createSuperformulaPrimitive(
		bw::core::Primitive::Operation op,
		bw::core::Primitive::FillRule fillRule,
		float values[6],
		float resolution,
		uint8_t layer,
		uint8_t priority,
		wp::Vector2 const& position,
		float scale,
		float angle);

} // editor
